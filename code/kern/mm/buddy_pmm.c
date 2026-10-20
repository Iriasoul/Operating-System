#include <pmm.h>
#include <list.h>
#include <buddy_pmm.h>
#include <stdio.h>

/*
 * Buddy system page allocator.
 *
 * An order-k block covers 2^k consecutive pages; its start offset relative
 * to pages[0] (= physical page nbase, the alignment origin fixed by the
 * framework) must be a multiple of 2^k. Two same-order blocks are buddies
 * iff their offsets differ by exactly 2^k, so only they span one aligned
 * 2^(k+1) parent block; physical adjacency alone is not buddyhood.
 *
 * Field usage, private to this manager:
 *   - free block head : PageProperty set, property = order, page_link
 *                       linked into buddy_free_lists[order], ref = 0
 *   - free block body : all fields zero, never linked
 *   - allocated block : head PG_property cleared, property = 0, not linked
 *   - reserved pages  : keep the PG_reserved state given by page_init()
 * An allocation of n pages rounds up to B = 2^ceil(log2(n)) pages and the
 * whole B-page block is occupied; free_pages recovers B from the original
 * request n, so callers must not pre-round.
 */

// orders 0..BUDDY_MAX_ORDER-1; the framework manages at most
// PPN(PHYSICAL_MEMORY_END) - nbase = 2^15 pages, so order 15 is the largest
// block ever addressable
#define BUDDY_MAX_ORDER 16

// one free list per order, plus the total number of free pages
static list_entry_t buddy_free_lists[BUDDY_MAX_ORDER];
static size_t nr_free;

// smallest order whose block covers n pages; BUDDY_MAX_ORDER means
// "unsupported" so callers never shift past the table bounds
static size_t
buddy_order_of(size_t n) {
    size_t order = 0, size = 1;
    while (size < n) {
        size <<= 1;
        order ++;
        if (order >= BUDDY_MAX_ORDER) {
            break;
        }
    }
    return order;
}

static void
buddy_init(void) {
    size_t i;
    for (i = 0; i < BUDDY_MAX_ORDER; i ++) {
        list_init(&buddy_free_lists[i]);
    }
    nr_free = 0;
}

static void
buddy_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);
    struct Page *p = base;
    for (; p != base + n; p ++) {
        assert(PageReserved(p));
        p->flags = p->property = 0;
        set_page_ref(p, 0);
    }
    nr_free += n;
    // carve [base, base+n) into maximal aligned power-of-two blocks so that
    // every page is covered exactly once; block k is capped by the pages
    // left, by the alignment of the running offset, and by BUDDY_MAX_ORDER
    size_t off = base - pages;
    while (n > 0) {
        size_t k = 0;
        while (k + 1 < BUDDY_MAX_ORDER && (n >> (k + 1)) > 0 &&
               ((off >> k) & 1) == 0) {
            k ++;
        }
        struct Page *blk = pages + off;
        blk->property = k;
        SetPageProperty(blk);
        list_add_before(&buddy_free_lists[k], &(blk->page_link));
        off += (size_t)1 << k;
        n -= (size_t)1 << k;
    }
}

static struct Page *
buddy_alloc_pages(size_t n) {
    assert(n > 0);
    if (n > nr_free) {
        return NULL;
    }
    size_t order = buddy_order_of(n);
    if (order >= BUDDY_MAX_ORDER) {
        return NULL;
    }
    // smallest order >= `order` that owns a free block; if none exists the
    // request fails even though the page total might suffice, because free
    // pages never combine across alignment boundaries
    size_t k = order;
    while (k < BUDDY_MAX_ORDER && list_empty(&buddy_free_lists[k])) {
        k ++;
    }
    if (k == BUDDY_MAX_ORDER) {
        return NULL;
    }
    struct Page *page = le2page(list_next(&buddy_free_lists[k]), page_link);
    list_del(&(page->page_link));
    // split downwards, parking each upper half as a free block of its order
    while (k > order) {
        k --;
        struct Page *buddy = page + ((size_t)1 << k);
        buddy->property = k;
        SetPageProperty(buddy);
        list_add_before(&buddy_free_lists[k], &(buddy->page_link));
    }
    ClearPageProperty(page);
    page->property = 0;
    nr_free -= (size_t)1 << order;
    return page;
}

static void
buddy_free_pages(struct Page *base, size_t n) {
    assert(n > 0);
    // recover the actually occupied block from the original request size
    size_t order = buddy_order_of(n);
    assert(order < BUDDY_MAX_ORDER);

    struct Page *p = base;
    for (; p != base + ((size_t)1 << order); p ++) {
        assert(!PageReserved(p) && !PageProperty(p));
        p->flags = p->property = 0;
        set_page_ref(p, 0);
    }
    nr_free += (size_t)1 << order;

    // coalesce with same-order free buddies as long as they exist; the XOR
    // partner is by construction the only true buddy of the current block
    size_t off = base - pages;
    while (order + 1 < BUDDY_MAX_ORDER) {
        size_t buddy_off = off ^ ((size_t)1 << order);
        if (buddy_off >= npage - nbase) {
            break;
        }
        struct Page *buddy = pages + buddy_off;
        if (!PageProperty(buddy) || buddy->property != order) {
            break;
        }
        list_del(&(buddy->page_link));
        ClearPageProperty(buddy);
        buddy->property = 0;
        if (buddy < base) {
            base = buddy;
        }
        off = base - pages;
        order ++;
    }
    base->property = order;
    SetPageProperty(base);
    list_add_before(&buddy_free_lists[order], &(base->page_link));
}

static size_t
buddy_nr_free_pages(void) {
    return nr_free;
}

// structural consistency only: every list node must be a valid, aligned,
// in-range block head of its order and the per-order page sums must equal
// the free counter; allocation and merging behavior are exercised by the
// dedicated tests, not here
static void
buddy_check(void) {
    size_t k, blocks = 0, total = 0;
    size_t nr_desc = npage - nbase;
    for (k = 0; k < BUDDY_MAX_ORDER; k ++) {
        list_entry_t *le = &buddy_free_lists[k];
        while ((le = list_next(le)) != &buddy_free_lists[k]) {
            struct Page *p = le2page(le, page_link);
            size_t off = p - pages;
            assert(PageProperty(p));
            assert(p->property == k);
            assert(!PageReserved(p));
            assert(p->ref == 0);
            assert(off < nr_desc);
            assert((off & (((size_t)1 << k) - 1)) == 0);
            blocks ++;
            total += (size_t)1 << k;
        }
    }
    assert(total == nr_free);
    assert(nr_free_pages() == nr_free);
    cprintf("buddy_check: %lu free blocks, %lu free pages, orders consistent\n",
            (unsigned long)blocks, (unsigned long)nr_free);
}

const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};
