#include <defs.h>
#include <list.h>
#include <string.h>
#include <stdio.h>
#include <pmm.h>
#include <buddy_pmm.h>
#include <slub.h>

#ifdef UCORE_SLUB

/*
 * Simplified SLUB object allocator on top of the buddy page allocator.
 *
 * Small objects (1..2048 bytes) are served from eight size classes
 * (16/32/64/128/256/512/1024/2048). Each slab is exactly one page taken
 * from the buddy allocator and laid out as:
 *
 *   +----------------+--------------------------------------+
 *   | slab header    | object slots, obj_size stride each   |
 *   | SLUB_SLAB_HDR  | first slot starts right after the    |
 *   | (16-aligned)   | header; every slot is 16-byte aligned|
 *   +----------------+--------------------------------------+
 *
 * The header is private to the allocator. Free slots chain themselves
 * through their own first word, so no live object byte is ever used as
 * metadata. Each class keeps two lists of slabs:
 *   partial - slabs owning at least one free slot (allocation source);
 *   full    - slabs without free slots (tracked so kfree can still move
 *             them back to partial when an object is released).
 * Taking the last slot of a slab moves it partial -> full; freeing any
 * slot of a full slab moves it back; freeing the last slot of a slab
 * returns the page to the buddy allocator immediately, so no empty slab
 * is ever kept cached.
 *
 * Large objects (> 2048 bytes) occupy one contiguous block obtained from
 * alloc_pages(). The block's first page holds a header recording the
 * original struct Page pointer and the original page request count, so
 * kfree can honour the buddy free contract (original base, original
 * request, no partial free) without the caller passing a size. The
 * payload starts at the fixed offset SLUB_LARGE_HDR < PGSIZE, therefore
 * ROUNDDOWN(ptr, PGSIZE) locates the header for every large object.
 */

#define SLUB_CLASS_COUNT    8
#define SLUB_MAX_SMALL      2048

/* distinguishing marks at offset 0 of a managed page */
#define SLUB_SLAB_MAGIC     0x534C4142u    /* "SLAB" */
#define SLUB_LARGE_MAGIC    0x534C4C47u    /* "SLLG" */

struct slub_class {
    size_t obj_size;
    size_t nr_partial;              /* slabs with free slots */
    size_t nr_full;                 /* completely allocated slabs */
    list_entry_t partial_list;
    list_entry_t full_list;
};

/* per-page metadata of a small slab, at offset 0 of the slab page */
struct slub_slab {
    size_t magic;
    uint16_t class_idx;             /* owning class index */
    uint16_t nr_free;               /* free slots left */
    uint16_t nr_total;              /* slots per page */
    void *freelist;                 /* chain of free slots */
    list_entry_t slab_link;         /* in partial_list or full_list */
};

/* per-block metadata of a large object, at offset 0 of its first page */
struct slub_large_head {
    size_t magic;
    struct slub_large_head *next;   /* all live large objects */
    struct Page *base;              /* original page pointer for free_pages */
    size_t nr_pages;                /* original page request for free_pages */
    size_t user_size;               /* requested bytes */
};

/* headers rounded up to 16 so every object slot stays 16-byte aligned */
#define SLUB_SLAB_HDR   (((sizeof(struct slub_slab) + 15) / 16) * 16)
#define SLUB_LARGE_HDR  (((sizeof(struct slub_large_head) + 15) / 16) * 16)

static const size_t slub_class_sizes[SLUB_CLASS_COUNT] = {
    16, 32, 64, 128, 256, 512, 1024, 2048,
};

static struct slub_class slub_classes[SLUB_CLASS_COUNT];
static struct slub_large_head *slub_large_live;
static size_t slub_large_count;
static int slub_initialized;

void
slub_init(void) {
    assert(!slub_initialized);
    assert(pmm_manager == &buddy_pmm_manager);
    size_t i;
    for (i = 0; i < SLUB_CLASS_COUNT; i ++) {
        struct slub_class *cls = &slub_classes[i];
        cls->obj_size = slub_class_sizes[i];
        cls->nr_partial = cls->nr_full = 0;
        list_init(&(cls->partial_list));
        list_init(&(cls->full_list));
        /* at least one object per page and 16-byte aligned slots */
        assert(cls->obj_size % 16 == 0);
        assert(cls->obj_size <= PGSIZE - SLUB_SLAB_HDR);
    }
    slub_large_live = NULL;
    slub_large_count = 0;
    slub_initialized = 1;
    cprintf("[SLUB] enabled: %d size classes (16..%d bytes), large objects page-backed\n",
            (int)SLUB_CLASS_COUNT, (int)SLUB_MAX_SMALL);
}

/* take one page from the buddy allocator and turn it into an empty slab */
static struct slub_slab *
slub_slab_grow(struct slub_class *cls) {
    struct Page *pg = alloc_pages(1);
    if (pg == NULL) {
        return NULL;
    }
    uintptr_t va = page2pa(pg) + va_pa_offset;
    struct slub_slab *slab = (struct slub_slab *)va;
    size_t cap = (PGSIZE - SLUB_SLAB_HDR) / cls->obj_size;
    assert(cap >= 1 && cap <= 0xFFFF);
    slab->magic = SLUB_SLAB_MAGIC;
    slab->class_idx = (uint16_t)(cls - slub_classes);
    slab->nr_total = (uint16_t)cap;
    slab->freelist = NULL;
    /* chain the slots so the lowest address is handed out first */
    size_t i = cap;
    while (i > 0) {
        void *slot = (void *)(va + SLUB_SLAB_HDR + (i - 1) * cls->obj_size);
        *(void **)slot = slab->freelist;
        slab->freelist = slot;
        i --;
    }
    slab->nr_free = (uint16_t)cap;
    list_add(&(cls->partial_list), &(slab->slab_link));
    cls->nr_partial ++;
    return slab;
}

static void *
slub_slab_alloc(struct slub_class *cls) {
    list_entry_t *le = list_next(&(cls->partial_list));
    if (le == &(cls->partial_list)) {
        if (slub_slab_grow(cls) == NULL) {
            return NULL;
        }
        le = list_next(&(cls->partial_list));
    }
    struct slub_slab *slab = to_struct(le, struct slub_slab, slab_link);
    void *obj = slab->freelist;
    assert(obj != NULL);
    slab->freelist = *(void **)obj;
    slab->nr_free --;
    if (slab->nr_free == 0) {
        list_del(&(slab->slab_link));
        list_add(&(cls->full_list), &(slab->slab_link));
        cls->nr_partial --;
        cls->nr_full ++;
    }
    return obj;
}

static void
slub_slab_free(struct slub_class *cls, struct slub_slab *slab, void *obj) {
    int was_full = (slab->nr_free == 0);
    *(void **)obj = slab->freelist;
    slab->freelist = obj;
    slab->nr_free ++;
    if (was_full) {
        list_del(&(slab->slab_link));
        list_add(&(cls->partial_list), &(slab->slab_link));
        cls->nr_full --;
        cls->nr_partial ++;
    }
    if (slab->nr_free == slab->nr_total) {
        /* last object gone: leave all SLUB structures first, then return
           the page with the original request 1; the header is not touched
           again after the page is handed back */
        list_del(&(slab->slab_link));
        cls->nr_partial --;
        slab->magic = 0;
        assert(((uintptr_t)slab & (PGSIZE - 1)) == 0);
        free_pages(pa2page(PADDR((uintptr_t)slab)), 1);
    }
}

void *
kmalloc(size_t size) {
    assert(slub_initialized);
    if (size == 0) {
        return NULL;
    }
    if (size <= SLUB_MAX_SMALL) {
        size_t i;
        for (i = 0; i < SLUB_CLASS_COUNT; i ++) {
            if (size <= slub_classes[i].obj_size) {
                return slub_slab_alloc(&slub_classes[i]);
            }
        }
        panic("slub: no class for size %lu", (unsigned long)size);
    }
    /* large path: every range check happens before any page is taken and
       before any metadata is written */
    if (size > (size_t)-1 - SLUB_LARGE_HDR) {
        return NULL;
    }
    size_t total = size + SLUB_LARGE_HDR;
    if (total > (size_t)-1 - (PGSIZE - 1)) {
        return NULL;
    }
    size_t nr = ROUNDUP(total, PGSIZE) / PGSIZE;
    struct Page *base = alloc_pages(nr);
    if (base == NULL) {
        return NULL;
    }
    uintptr_t va = page2pa(base) + va_pa_offset;
    struct slub_large_head *head = (struct slub_large_head *)va;
    head->magic = SLUB_LARGE_MAGIC;
    head->next = slub_large_live;
    head->base = base;
    head->nr_pages = nr;
    head->user_size = size;
    slub_large_live = head;
    slub_large_count ++;
    return (void *)(va + SLUB_LARGE_HDR);
}

void
kfree(void *ptr) {
    assert(slub_initialized);
    if (ptr == NULL) {
        return;
    }
    /* slab and large headers both start at offset 0 of their first page,
       and the payload always lies inside that first page (fixed offset
       < PGSIZE), so rounding the original pointer down recovers it */
    uintptr_t page_va = ROUNDDOWN((uintptr_t)ptr, PGSIZE);
    struct slub_slab *slab = (struct slub_slab *)page_va;
    if (slab->magic == SLUB_SLAB_MAGIC) {
        assert(slab->class_idx < SLUB_CLASS_COUNT);
        slub_slab_free(&slub_classes[slab->class_idx], slab, ptr);
        return;
    }
    struct slub_large_head *head = (struct slub_large_head *)page_va;
    assert(head->magic == SLUB_LARGE_MAGIC);
    /* remove the management record before handing the block back */
    struct slub_large_head **pp = &slub_large_live;
    while (*pp != NULL && *pp != head) {
        pp = &(*pp)->next;
    }
    assert(*pp == head);
    *pp = head->next;
    slub_large_count --;
    head->magic = 0;
    free_pages(head->base, head->nr_pages);
}

static void
slub_slab_check(struct slub_class *cls, struct slub_slab *slab, int in_partial) {
    uintptr_t base_va = (uintptr_t)slab;
    assert((base_va & (PGSIZE - 1)) == 0);
    /* range-check the page number in the integer domain first; only a
       legal page number may later be converted or used for descriptor
       arithmetic (pa2page would underflow its index below nbase) */
    assert(base_va >= KERNBASE);
    size_t ppn = (base_va - va_pa_offset) >> PGSHIFT;
    assert(ppn >= nbase && ppn < npage);
    assert(slab->magic == SLUB_SLAB_MAGIC);
    assert(slab->class_idx == (uint16_t)(cls - slub_classes));
    size_t cap = (PGSIZE - SLUB_SLAB_HDR) / cls->obj_size;
    assert(slab->nr_total == cap);
    assert(SLUB_SLAB_HDR + (size_t)slab->nr_total * cls->obj_size <= PGSIZE);
    if (in_partial) {
        assert(slab->nr_free >= 1 && slab->nr_free < slab->nr_total);
    } else {
        assert(slab->nr_free == 0);
    }
    /* every node of the free chain must be a distinct, legal slot */
    static unsigned char seen[PGSIZE / 16];
    memset(seen, 0, sizeof(seen));
    size_t count = 0;
    void *slot = slab->freelist;
    while (slot != NULL) {
        uintptr_t off = (uintptr_t)slot - base_va;
        assert(off >= SLUB_SLAB_HDR);
        /* subtraction of constants instead of an addition that could wrap */
        assert(off <= PGSIZE - cls->obj_size);
        assert((off - SLUB_SLAB_HDR) % cls->obj_size == 0);
        size_t idx = (off - SLUB_SLAB_HDR) / cls->obj_size;
        assert(idx < slab->nr_total);
        assert(seen[idx] == 0);
        seen[idx] = 1;
        count ++;
        assert(count <= slab->nr_total);    /* cycle/duplicate bound */
        slot = *(void **)slot;
    }
    assert(count == slab->nr_free);
}

static void
slub_class_check(struct slub_class *cls, list_entry_t *head, int in_partial) {
    list_entry_t *le = list_next(head);
    size_t count = 0;
    while (le != head) {
        struct slub_slab *slab = to_struct(le, struct slub_slab, slab_link);
        slub_slab_check(cls, slab, in_partial);
        count ++;
        assert(count <= cls->nr_partial + cls->nr_full);    /* cycle bound */
        le = list_next(le);
    }
    if (in_partial) {
        assert(count == cls->nr_partial);
    } else {
        assert(count == cls->nr_full);
    }
}

void
slub_check(void) {
    assert(slub_initialized);
    size_t i, slabs = 0, partial = 0, full = 0;
    for (i = 0; i < SLUB_CLASS_COUNT; i ++) {
        struct slub_class *cls = &slub_classes[i];
        assert(cls->obj_size % 16 == 0 && cls->obj_size <= SLUB_MAX_SMALL);
        slub_class_check(cls, &(cls->partial_list), 1);
        slub_class_check(cls, &(cls->full_list), 0);
        partial += cls->nr_partial;
        full += cls->nr_full;
    }
    slabs = partial + full;
    struct slub_large_head *head = slub_large_live;
    size_t large = 0;
    while (head != NULL) {
        assert(head->magic == SLUB_LARGE_MAGIC);
        assert(head->base != NULL);
        assert(head->user_size > SLUB_MAX_SMALL);
        assert(head->nr_pages >= 1);
        /* re-derive the recorded request with the allocation path's
           overflow guards before any addition or roundup */
        assert(head->user_size <= (size_t)-1 - SLUB_LARGE_HDR);
        size_t total = head->user_size + SLUB_LARGE_HDR;
        assert(total <= (size_t)-1 - (PGSIZE - 1));
        assert(head->nr_pages == ROUNDUP(total, PGSIZE) / PGSIZE);
        /* a successful request can never exceed the managed total; this
           also bounds the power-of-two rounding below */
        assert(head->nr_pages <= npage - nbase);
        /* buddy rounds the request up to a power-of-two occupied block */
        size_t actual = 1;
        while (actual < head->nr_pages) {
            actual <<= 1;
        }
        /* validate the block's page number in the integer domain before
           any conversion or descriptor arithmetic */
        uintptr_t block_va = ROUNDDOWN((uintptr_t)head, PGSIZE);
        size_t base_ppn = (block_va - va_pa_offset) >> PGSHIFT;
        assert(base_ppn >= nbase && base_ppn < npage);
        size_t start = base_ppn - nbase;
        size_t nr_desc = npage - nbase;
        /* occupied length must fit the descriptors left from the start,
           expressed without computing the block's end point */
        assert(actual <= nr_desc - start);
        /* a buddy block of `actual` pages starts at 0 (mod actual) */
        assert((start & (actual - 1)) == 0);
        /* only now may the record's descriptor be converted: it must be
           the first page of the very block the header lives in */
        assert(head->base == pa2page(block_va - va_pa_offset));
        large ++;
        assert(large <= slub_large_count);  /* cycle bound */
        head = head->next;
    }
    /* no record may be missing from the list while the counter stays high */
    assert(large == slub_large_count);
    cprintf("slub_check: %lu slabs (%lu partial, %lu full), %lu large objects, consistent\n",
            (unsigned long)slabs, (unsigned long)partial, (unsigned long)full,
            (unsigned long)large);
}

#endif /* UCORE_SLUB */
