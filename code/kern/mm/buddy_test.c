#include <buddy_pmm.h>
#include <defs.h>
#include <list.h>
#include <memlayout.h>
#include <pmm.h>
#include <stdio.h>

#ifdef UCORE_BUDDY_TEST

/*
 * Behavior tests for the buddy system page allocator.
 *
 * Build: PMM=buddy BUDDY_TEST=1 (passes -D UCORE_BUDDY_TEST, see Makefile);
 * in every other configuration this file compiles to nothing and pmm.c does
 * not call into it.
 *
 * The tests drive the real allocator only through its public interface: the
 * buddy_pmm_manager callbacks on a controlled Page array, and the public
 * alloc_pages/free_pages/nr_free_pages wrappers on the real memory. Every
 * expected result (occupancy sizes, page counts, offsets) is written out
 * explicitly from the buddy contract; no allocator-internal helper (order
 * rounding, buddy lookup) is called to compute answers.
 *
 * pmm_init() calls buddy_run_fixture_tests() before page_init(). The fixture
 * swaps the global pages/npage to a static descriptor array that stands for
 * no real RAM, so fixture descriptors are never dereferenced as memory; on
 * return the globals are restored and the manager is left empty for the real
 * page_init(). buddy_run_live_tests() runs after page_init() and
 * pmm_manager->check(), and accesses real allocated pages through page2pa().
 *
 * Every case prints its own PASS line. A violated expectation prints a
 * "[Buddy Test] FAIL" line and panics, so the final "[Buddy Test] ALL TESTS
 * PASSED" marker can only appear after all cases and the state restoration
 * succeeded.
 */

#define BT_NR_PAGES 128

#define BT_FAIL(fmt, ...)                                                      \
    do {                                                                       \
        cprintf("[Buddy Test] FAIL: %s:%d in %s: " fmt "\n",                   \
                __FILE__, __LINE__, __func__, ##__VA_ARGS__);                  \
        panic("buddy_test aborted");                                           \
    } while (0)

#define BT_CHECK(cond, fmt, ...)                                               \
    do {                                                                       \
        if (!(cond)) {                                                         \
            BT_FAIL("check `" #cond "`: " fmt, ##__VA_ARGS__);                 \
        }                                                                      \
    } while (0)

// controlled descriptor zone swapped into the global pages/npage during the
// fixture tests; all bookkeeping lives in static storage so no case can
// exhaust the kernel stack
static struct Page bt_pages[BT_NR_PAGES];
static struct Page *bt_single[BT_NR_PAGES];  // exhaustion bookkeeping
static struct Page *bt_by_off[BT_NR_PAGES];  // offset -> returned pointer
static unsigned char bt_seen[BT_NR_PAGES];
static struct Page *bt_saved_pages;
static size_t bt_saved_npage;
static int bt_case_count;

static void
bt_case_begin(const char *name) {
    bt_case_count ++;
    cprintf("[Buddy Test] case %d: %s\n", bt_case_count, name);
}

static void
bt_case_pass(void) {
    cprintf("[Buddy Test] case %d PASS\n", bt_case_count);
}

static size_t
bt_free(void) {
    return buddy_pmm_manager.nr_free_pages();
}

// do the index arithmetic on an allocator-returned pointer only after it is
// proven to point into the fixture array
static size_t
bt_offset_of(struct Page *p) {
    BT_CHECK(p >= bt_pages && p < bt_pages + BT_NR_PAGES,
             "returned pointer 0x%016lx is outside the fixture", (unsigned long)p);
    return (size_t)(p - bt_pages);
}

static int
bt_overlap(size_t o1, size_t n1, size_t o2, size_t n2) {
    return o1 < o2 + n2 && o2 < o1 + n1;
}

// fresh fixture: all descriptors Reserved, manager empty
static void
bt_zone_reset(void) {
    size_t i;
    for (i = 0; i < BT_NR_PAGES; i ++) {
        struct Page *p = &bt_pages[i];
        p->flags = p->property = 0;
        set_page_ref(p, 0);
        SetPageReserved(p);
        list_init(&(p->page_link));
    }
    buddy_pmm_manager.init();
    BT_CHECK(bt_free() == 0, "manager not empty after re-init");
}

static void
bt_register(size_t off, size_t n) {
    BT_CHECK(off + n <= BT_NR_PAGES, "registration leaves the fixture");
    buddy_pmm_manager.init_memmap(&bt_pages[off], n);
}

// drain the zone with single-page requests and collect the returned
// pointers; every offset must be unique and in range, and the free count
// must drop by exactly one per request
static void
bt_exhaust_singles(size_t expect) {
    size_t i;
    for (i = 0; i < BT_NR_PAGES; i ++) {
        bt_seen[i] = 0;
    }
    for (i = 0; i < expect; i ++) {
        struct Page *p = buddy_pmm_manager.alloc_pages(1);
        BT_CHECK(p != NULL, "single-page request %lu failed early", (unsigned long)i);
        size_t off = bt_offset_of(p);
        BT_CHECK(bt_seen[off] == 0, "offset %lu handed out twice", (unsigned long)off);
        bt_seen[off] = 1;
        bt_single[i] = p;
        BT_CHECK(bt_free() == expect - (i + 1), "free count wrong after %lu single pages",
                 (unsigned long)(i + 1));
    }
    BT_CHECK(buddy_pmm_manager.alloc_pages(1) == NULL,
             "zone not exhausted after %lu single pages", (unsigned long)expect);
}

static void
bt_release_singles(size_t expect) {
    size_t i;
    for (i = 0; i < expect; i ++) {
        size_t before = bt_free();
        buddy_pmm_manager.free_pages(bt_single[i], 1);
        BT_CHECK(bt_free() == before + 1,
                 "freeing single page %lu changed the count by more than 1", (unsigned long)i);
    }
}

// ---- init region coverage -------------------------------------------------

// register [0,128): every page allocatable exactly once, then NULL, then
// full restoration
static void
bt_case_init_full(void) {
    bt_case_begin("init cover: whole 128-page zone");
    bt_zone_reset();
    bt_register(0, BT_NR_PAGES);
    BT_CHECK(bt_free() == BT_NR_PAGES, "registration must account %d pages", BT_NR_PAGES);
    bt_exhaust_singles(BT_NR_PAGES);
    bt_release_singles(BT_NR_PAGES);
    BT_CHECK(bt_free() == BT_NR_PAGES, "free count not restored");
    buddy_pmm_manager.check();
    bt_case_pass();
}

// register [3,120): exhaustion must cover exactly offsets 3..119; pages
// outside keep their Reserved state
static void
bt_case_init_offset(void) {
    bt_case_begin("init cover: sub-range [3,120), 117 pages");
    bt_zone_reset();
    bt_register(3, 117);
    BT_CHECK(bt_free() == 117, "registration must account 117 pages");
    bt_exhaust_singles(117);
    size_t i;
    for (i = 0; i < BT_NR_PAGES; i ++) {
        int inside = (i >= 3 && i < 120);
        BT_CHECK(bt_seen[i] == inside, "offset %lu coverage mismatch", (unsigned long)i);
        if (!inside) {
            BT_CHECK(PageReserved(&bt_pages[i]),
                     "page outside the region lost its reserved state");
        }
    }
    bt_release_singles(117);
    BT_CHECK(bt_free() == 117, "free count not restored");
    buddy_pmm_manager.check();
    bt_case_pass();
}

// register only offset 7: allocatable once, rest of the fixture untouched
static void
bt_case_init_single(void) {
    bt_case_begin("init cover: single page at offset 7");
    bt_zone_reset();
    bt_register(7, 1);
    BT_CHECK(bt_free() == 1, "registration must account 1 page");
    struct Page *p = buddy_pmm_manager.alloc_pages(1);
    BT_CHECK(p != NULL && bt_offset_of(p) == 7, "first request must return offset 7");
    BT_CHECK(bt_free() == 0, "zone must be empty after taking the page");
    BT_CHECK(buddy_pmm_manager.alloc_pages(1) == NULL, "empty zone must fail");
    BT_CHECK(buddy_pmm_manager.alloc_pages(2) == NULL, "empty zone must fail for 2 pages");
    buddy_pmm_manager.free_pages(p, 1);
    BT_CHECK(bt_free() == 1, "free count not restored");
    p = buddy_pmm_manager.alloc_pages(1);
    BT_CHECK(p != NULL && bt_offset_of(p) == 7, "page must be reusable after free");
    buddy_pmm_manager.free_pages(p, 1);
    BT_CHECK(bt_free() == 1, "free count not restored");
    BT_CHECK(PageReserved(&bt_pages[6]) && PageReserved(&bt_pages[8]),
             "pages outside the region lost their reserved state");
    buddy_pmm_manager.check();
    bt_case_pass();
}

// ---- rounding and failure invariance ---------------------------------------

// explicit contract table: a request of n pages occupies B pages; the values
// are written out here, not derived from any allocator helper
static const struct {
    size_t req, occ;
} bt_rounding[] = {
    {1, 1}, {2, 2}, {3, 4}, {4, 4}, {5, 8}, {7, 8}, {8, 8}, {9, 16},
};
#define BT_NR_ROUNDING (sizeof(bt_rounding) / sizeof(bt_rounding[0]))

static void
bt_case_rounding(void) {
    bt_case_begin("rounding: occupancy 1,2,4,4,8,8,8,16 with disjoint ranges");
    bt_zone_reset();
    bt_register(0, BT_NR_PAGES);
    struct Page *blk[BT_NR_ROUNDING];
    size_t off[BT_NR_ROUNDING];
    size_t i, k;
    for (i = 0; i < BT_NR_ROUNDING; i ++) {
        size_t before = bt_free();
        blk[i] = buddy_pmm_manager.alloc_pages(bt_rounding[i].req);
        BT_CHECK(blk[i] != NULL, "request for %lu pages unexpectedly failed",
                 (unsigned long)bt_rounding[i].req);
        off[i] = bt_offset_of(blk[i]);
        BT_CHECK(bt_free() + bt_rounding[i].occ == before,
                 "request for %lu pages changed the count by %lu, expected %lu",
                 (unsigned long)bt_rounding[i].req, (unsigned long)(before - bt_free()),
                 (unsigned long)bt_rounding[i].occ);
        BT_CHECK(off[i] % bt_rounding[i].occ == 0,
                 "block for %lu pages starts at offset %lu, not %lu-aligned",
                 (unsigned long)bt_rounding[i].req, (unsigned long)off[i],
                 (unsigned long)bt_rounding[i].occ);
    }
    // the retained allocations must not overlap each other
    for (i = 0; i < BT_NR_PAGES; i ++) {
        bt_seen[i] = 0;
    }
    for (i = 0; i < BT_NR_ROUNDING; i ++) {
        for (k = 0; k < bt_rounding[i].occ; k ++) {
            BT_CHECK(off[i] + k < BT_NR_PAGES && bt_seen[off[i] + k] == 0,
                     "allocations overlap at offset %lu", (unsigned long)(off[i] + k));
            bt_seen[off[i] + k] = 1;
        }
    }
    // restore with the ORIGINAL request sizes, in an order different from
    // the allocation order
    static const size_t free_order[] = {5, 0, 7, 2, 6, 1, 4, 3};
    for (i = 0; i < BT_NR_ROUNDING; i ++) {
        k = free_order[i];
        size_t before = bt_free();
        buddy_pmm_manager.free_pages(blk[k], bt_rounding[k].req);
        BT_CHECK(bt_free() == before + bt_rounding[k].occ,
                 "freeing a request of %lu pages restored %lu, expected %lu",
                 (unsigned long)bt_rounding[k].req, (unsigned long)(bt_free() - before),
                 (unsigned long)bt_rounding[k].occ);
    }
    BT_CHECK(bt_free() == BT_NR_PAGES, "free count not restored");
    buddy_pmm_manager.check();
    bt_case_pass();
}

static void
bt_case_failure_invariance(void) {
    bt_case_begin("failure invariance: oversized and overflow requests");
    bt_zone_reset();
    bt_register(0, BT_NR_PAGES);
    size_t before = bt_free();
    BT_CHECK(buddy_pmm_manager.alloc_pages(BT_NR_PAGES + 1) == NULL,
             "request above the total must fail");
    BT_CHECK(buddy_pmm_manager.alloc_pages(32769) == NULL, "32769 pages must fail");
    BT_CHECK(buddy_pmm_manager.alloc_pages((size_t)-1) == NULL, "(size_t)-1 must fail");
    BT_CHECK(bt_free() == before, "failed requests changed the free count");
    // a pure counter check misses list corruption, so prove the allocator
    // still hands out and takes back pages after the failures
    struct Page *p = buddy_pmm_manager.alloc_pages(1);
    BT_CHECK(p != NULL, "allocator unusable after failed requests");
    bt_offset_of(p);
    buddy_pmm_manager.free_pages(p, 1);
    BT_CHECK(bt_free() == before, "free count not restored");
    buddy_pmm_manager.check();
    bt_case_pass();
}

// ---- splitting and cross-order merging -------------------------------------

static void
bt_case_split_merge_orders(void) {
    bt_case_begin("split/merge: mixed orders, out-of-order free");
    bt_zone_reset();
    bt_register(0, BT_NR_PAGES);
    size_t before = bt_free();
    struct Page *p16 = buddy_pmm_manager.alloc_pages(16);
    BT_CHECK(p16 != NULL && bt_free() == before - 16,
             "a 16-page request must occupy 16 pages");
    size_t off16 = bt_offset_of(p16);
    BT_CHECK(off16 % 16 == 0, "16-page block at offset %lu not 16-aligned", (unsigned long)off16);
    before = bt_free();
    struct Page *p3 = buddy_pmm_manager.alloc_pages(3);
    BT_CHECK(p3 != NULL && bt_free() == before - 4, "a 3-page request must occupy 4 pages");
    size_t off3 = bt_offset_of(p3);
    BT_CHECK(off3 % 4 == 0, "4-page block at offset %lu not 4-aligned", (unsigned long)off3);
    before = bt_free();
    struct Page *p1 = buddy_pmm_manager.alloc_pages(1);
    BT_CHECK(p1 != NULL && bt_free() == before - 1, "a 1-page request must occupy 1 page");
    size_t off1 = bt_offset_of(p1);
    BT_CHECK(bt_free() == BT_NR_PAGES - 16 - 4 - 1, "unexpected total after three allocations");
    BT_CHECK(!bt_overlap(off16, 16, off3, 4) && !bt_overlap(off16, 16, off1, 1)
             && !bt_overlap(off3, 4, off1, 1), "retained allocations overlap");
    // free in an order different from allocation: 3, 16, 1
    before = bt_free();
    buddy_pmm_manager.free_pages(p3, 3);
    BT_CHECK(bt_free() == before + 4, "freeing the 3-page request must restore 4 pages");
    before = bt_free();
    buddy_pmm_manager.free_pages(p16, 16);
    BT_CHECK(bt_free() == before + 16, "freeing the 16-page request must restore 16 pages");
    before = bt_free();
    buddy_pmm_manager.free_pages(p1, 1);
    BT_CHECK(bt_free() == before + 1, "freeing the 1-page request must restore 1 page");
    BT_CHECK(bt_free() == BT_NR_PAGES, "zone not whole after freeing everything");
    // only a complete merge chain can satisfy this request
    struct Page *whole = buddy_pmm_manager.alloc_pages(BT_NR_PAGES);
    BT_CHECK(whole != NULL, "128 pages not recoverable after freeing all blocks");
    BT_CHECK(bt_offset_of(whole) == 0, "recovered 128-page block must start at offset 0");
    BT_CHECK(bt_free() == 0, "zone must be exhausted by the 128-page block");
    buddy_pmm_manager.free_pages(whole, BT_NR_PAGES);
    BT_CHECK(bt_free() == BT_NR_PAGES, "free count not restored");
    buddy_pmm_manager.check();
    bt_case_pass();
}

static void
bt_case_split_merge_singles(void) {
    bt_case_begin("split/merge: 128 singles, shuffled free, full recovery");
    bt_zone_reset();
    bt_register(0, BT_NR_PAGES);
    size_t i;
    for (i = 0; i < BT_NR_PAGES; i ++) {
        bt_seen[i] = 0;
    }
    for (i = 0; i < BT_NR_PAGES; i ++) {
        struct Page *p = buddy_pmm_manager.alloc_pages(1);
        BT_CHECK(p != NULL, "single-page request %lu failed early", (unsigned long)i);
        size_t off = bt_offset_of(p);
        BT_CHECK(bt_seen[off] == 0, "offset %lu handed out twice", (unsigned long)off);
        bt_seen[off] = 1;
        bt_by_off[off] = p;
    }
    BT_CHECK(bt_free() == 0, "zone not exhausted");
    // fixed permutation of the offsets: (i * 73 + 41) mod 128 is a bijection
    // because 73 is odd and coprime with 128
    for (i = 0; i < BT_NR_PAGES; i ++) {
        size_t off = (i * 73 + 41) % BT_NR_PAGES;
        size_t before = bt_free();
        buddy_pmm_manager.free_pages(bt_by_off[off], 1);
        BT_CHECK(bt_free() == before + 1,
                 "freeing page %lu changed the count by more than 1", (unsigned long)off);
    }
    BT_CHECK(bt_free() == BT_NR_PAGES, "free count not restored");
    struct Page *whole = buddy_pmm_manager.alloc_pages(BT_NR_PAGES);
    BT_CHECK(whole != NULL, "recursive merge chain incomplete: 128 pages not recoverable");
    BT_CHECK(bt_offset_of(whole) == 0, "recovered 128-page block must start at offset 0");
    buddy_pmm_manager.free_pages(whole, BT_NR_PAGES);
    BT_CHECK(bt_free() == BT_NR_PAGES, "free count not restored");
    buddy_pmm_manager.check();
    bt_case_pass();
}

// ---- adjacency is not buddyhood ---------------------------------------------

static void
bt_case_adjacent_not_buddy(void) {
    bt_case_begin("adjacency: neighbours 1,2 must not merge");
    bt_zone_reset();
    bt_register(0, 4);
    struct Page *q = buddy_pmm_manager.alloc_pages(4);
    BT_CHECK(q != NULL && bt_offset_of(q) == 0, "whole 4-page zone must come back at offset 0");
    BT_CHECK(bt_free() == 0, "zone must be exhausted");
    // free the MIDDLE pair: offsets 1 and 2 are adjacent but not buddies
    buddy_pmm_manager.free_pages(&bt_pages[1], 1);
    BT_CHECK(bt_free() == 1, "first free must add exactly one page");
    buddy_pmm_manager.free_pages(&bt_pages[2], 1);
    BT_CHECK(bt_free() == 2, "second free must add exactly one page");
    BT_CHECK(buddy_pmm_manager.alloc_pages(2) == NULL,
             "adjacent non-buddy singles must not satisfy a 2-page request");
    BT_CHECK(bt_free() == 2, "failed request must not change the free count");
    struct Page *s1 = buddy_pmm_manager.alloc_pages(1);
    BT_CHECK(s1 != NULL, "first single page must remain allocatable after the failed request");
    size_t o1 = bt_offset_of(s1);
    BT_CHECK(o1 == 1 || o1 == 2, "returned offset %lu outside the freed pair", (unsigned long)o1);
    struct Page *s2 = buddy_pmm_manager.alloc_pages(1);
    BT_CHECK(s2 != NULL, "second single page must remain allocatable");
    size_t o2 = bt_offset_of(s2);
    BT_CHECK((o1 == 1 && o2 == 2) || (o1 == 2 && o2 == 1),
             "second allocation must return the other freed page");
    buddy_pmm_manager.free_pages(s1, 1);
    buddy_pmm_manager.free_pages(s2, 1);
    BT_CHECK(bt_free() == 2, "free count not restored");
    buddy_pmm_manager.check();
    bt_case_pass();
}

static void
bt_case_buddy_pair(void) {
    bt_case_begin("buddyhood: neighbours 0,1 must merge");
    bt_zone_reset();
    bt_register(0, 4);
    struct Page *q = buddy_pmm_manager.alloc_pages(4);
    BT_CHECK(q != NULL && bt_offset_of(q) == 0, "whole 4-page zone must come back at offset 0");
    // free the LOW pair: offsets 0 and 1 are true buddies
    buddy_pmm_manager.free_pages(&bt_pages[0], 1);
    BT_CHECK(bt_free() == 1, "first free must add exactly one page");
    buddy_pmm_manager.free_pages(&bt_pages[1], 1);
    BT_CHECK(bt_free() == 2, "second free must add exactly one page");
    struct Page *two = buddy_pmm_manager.alloc_pages(2);
    BT_CHECK(two != NULL, "true buddies 0,1 must satisfy a 2-page request");
    BT_CHECK(bt_offset_of(two) == 0, "merged order-1 block must start at offset 0");
    BT_CHECK(bt_free() == 0, "zone must be exhausted by the 2-page block");
    buddy_pmm_manager.free_pages(two, 2);
    BT_CHECK(bt_free() == 2, "freeing the 2-page request must restore 2 pages");
    // reassemble the whole zone: 2,3 merge first, then join the block at 0
    buddy_pmm_manager.free_pages(&bt_pages[2], 1);
    buddy_pmm_manager.free_pages(&bt_pages[3], 1);
    BT_CHECK(bt_free() == 4, "zone not whole after freeing everything");
    struct Page *q2 = buddy_pmm_manager.alloc_pages(4);
    BT_CHECK(q2 != NULL && bt_offset_of(q2) == 0, "whole zone must be recoverable at offset 0");
    buddy_pmm_manager.free_pages(q2, 4);
    BT_CHECK(bt_free() == 4, "free count not restored");
    buddy_pmm_manager.check();
    bt_case_pass();
}

// ---- fragmentation -----------------------------------------------------------

static void
bt_case_fragmentation(void) {
    bt_case_begin("fragmentation: 64 free pages, no 2-page block");
    bt_zone_reset();
    bt_register(0, BT_NR_PAGES);
    size_t i;
    for (i = 0; i < BT_NR_PAGES; i ++) {
        bt_seen[i] = 0;
    }
    for (i = 0; i < BT_NR_PAGES; i ++) {
        struct Page *p = buddy_pmm_manager.alloc_pages(1);
        BT_CHECK(p != NULL, "single-page request %lu failed early", (unsigned long)i);
        size_t off = bt_offset_of(p);
        BT_CHECK(bt_seen[off] == 0, "offset %lu handed out twice", (unsigned long)off);
        bt_seen[off] = 1;
        bt_by_off[off] = p;
    }
    BT_CHECK(bt_free() == 0, "zone not exhausted");
    // punch holes: free every EVEN offset, 64 pages in total
    for (i = 0; i < BT_NR_PAGES; i += 2) {
        size_t before = bt_free();
        buddy_pmm_manager.free_pages(bt_by_off[i], 1);
        BT_CHECK(bt_free() == before + 1,
                 "freeing page %lu changed the count by more than 1", (unsigned long)i);
    }
    BT_CHECK(bt_free() == BT_NR_PAGES / 2, "expected 64 free pages");
    BT_CHECK(buddy_pmm_manager.alloc_pages(2) == NULL,
             "64 hole pages must not contain any 2-page block");
    BT_CHECK(bt_free() == BT_NR_PAGES / 2, "failed request must not change the free count");
    // the allocator itself must still work for single pages
    struct Page *s = buddy_pmm_manager.alloc_pages(1);
    BT_CHECK(s != NULL, "single pages must remain allocatable");
    BT_CHECK(bt_offset_of(s) % 2 == 0, "only even offsets were freed");
    buddy_pmm_manager.free_pages(s, 1);
    // release the rest: the holes must reassemble completely
    for (i = 1; i < BT_NR_PAGES; i += 2) {
        size_t before = bt_free();
        buddy_pmm_manager.free_pages(bt_by_off[i], 1);
        BT_CHECK(bt_free() == before + 1,
                 "freeing page %lu changed the count by more than 1", (unsigned long)i);
    }
    BT_CHECK(bt_free() == BT_NR_PAGES, "free count not restored");
    struct Page *whole = buddy_pmm_manager.alloc_pages(BT_NR_PAGES);
    BT_CHECK(whole != NULL && bt_offset_of(whole) == 0,
             "all-hole page set must reassemble into one 128-page block");
    buddy_pmm_manager.free_pages(whole, BT_NR_PAGES);
    BT_CHECK(bt_free() == BT_NR_PAGES, "free count not restored");
    buddy_pmm_manager.check();
    bt_case_pass();
}

// ---- real memory --------------------------------------------------------------

// offset of a live allocation inside the real descriptor array; the pointer
// is range-checked before any arithmetic
static size_t
bt_live_offset_of(struct Page *p) {
    size_t nr_desc = npage - nbase;
    BT_CHECK(p >= pages && p < pages + nr_desc, "live allocation outside the managed array");
    return (size_t)(p - pages);
}

// write a deterministic pattern into the real pages behind an allocation;
// every (tag, word) pair maps to a distinct value, so a later verify detects
// any cross-overwrite between blocks
static void
bt_fill(struct Page *base, size_t n, unsigned int tag) {
    unsigned int *va = (unsigned int *)(page2pa(base) + PHYSICAL_MEMORY_OFFSET);
    size_t words = n * (PGSIZE / sizeof(unsigned int));
    size_t i;
    for (i = 0; i < words; i ++) {
        va[i] = tag ^ (unsigned int)i;
    }
}

static void
bt_verify(struct Page *base, size_t n, unsigned int tag) {
    unsigned int *va = (unsigned int *)(page2pa(base) + PHYSICAL_MEMORY_OFFSET);
    size_t words = n * (PGSIZE / sizeof(unsigned int));
    size_t i;
    for (i = 0; i < words; i ++) {
        BT_CHECK(va[i] == (tag ^ (unsigned int)i),
                 "page content mismatch at word %lu of block tagged 0x%08x",
                 (unsigned long)i, tag);
    }
}

static void
bt_case_live_alloc(void) {
    bt_case_begin("live: allocate 3/5/1, write, verify, free");
    size_t initial = nr_free_pages();
    size_t nr_desc = npage - nbase;
    size_t before = initial;
    struct Page *a = alloc_pages(3);
    BT_CHECK(a != NULL, "3-page live allocation failed");
    BT_CHECK(nr_free_pages() == before - 4, "a 3-page request must occupy 4 pages");
    before = nr_free_pages();
    struct Page *b = alloc_pages(5);
    BT_CHECK(b != NULL, "5-page live allocation failed");
    BT_CHECK(nr_free_pages() == before - 8, "a 5-page request must occupy 8 pages");
    before = nr_free_pages();
    struct Page *c = alloc_pages(1);
    BT_CHECK(c != NULL, "1-page live allocation failed");
    BT_CHECK(nr_free_pages() == before - 1, "a 1-page request must occupy 1 page");

    size_t off_a = bt_live_offset_of(a), off_b = bt_live_offset_of(b),
           off_c = bt_live_offset_of(c);
    BT_CHECK(off_a % 4 == 0, "4-page block at offset %lu not 4-aligned", (unsigned long)off_a);
    BT_CHECK(off_b % 8 == 0, "8-page block at offset %lu not 8-aligned", (unsigned long)off_b);
    BT_CHECK(off_a + 4 <= nr_desc && off_b + 8 <= nr_desc && off_c + 1 <= nr_desc,
             "live allocation out of the managed range");
    BT_CHECK(!bt_overlap(off_a, 4, off_b, 8) && !bt_overlap(off_a, 4, off_c, 1)
             && !bt_overlap(off_b, 8, off_c, 1), "live allocations overlap");

    // write distinct patterns into the real pages, then verify all blocks:
    // a cross-overwrite between allocations would corrupt one of them
    bt_fill(a, 4, 0x11111111u);
    bt_fill(b, 8, 0x22222222u);
    bt_fill(c, 1, 0x33333333u);
    bt_verify(a, 4, 0x11111111u);
    bt_verify(b, 8, 0x22222222u);
    bt_verify(c, 1, 0x33333333u);

    // free with the original request sizes, in a different order
    before = nr_free_pages();
    free_pages(b, 5);
    BT_CHECK(nr_free_pages() == before + 8, "freeing the 5-page request must restore 8 pages");
    before = nr_free_pages();
    free_pages(a, 3);
    BT_CHECK(nr_free_pages() == before + 4, "freeing the 3-page request must restore 4 pages");
    before = nr_free_pages();
    free_pages(c, 1);
    BT_CHECK(nr_free_pages() == before + 1, "freeing the 1-page request must restore 1 page");
    BT_CHECK(nr_free_pages() == initial, "free count not restored");
    bt_case_pass();
}

static void
bt_case_live_reuse(void) {
    bt_case_begin("live: reuse freed pages, no zero assumption");
    size_t before = nr_free_pages();
    struct Page *p = alloc_pages(2);
    BT_CHECK(p != NULL, "2-page live allocation failed");
    BT_CHECK(nr_free_pages() == before - 2, "a 2-page request must occupy 2 pages");
    size_t off = bt_live_offset_of(p);
    BT_CHECK(off % 2 == 0, "2-page block at offset %lu not 2-aligned", (unsigned long)off);
    // write first, read back: nothing may be assumed about leftover content
    bt_fill(p, 2, 0x44444444u);
    bt_verify(p, 2, 0x44444444u);
    free_pages(p, 2);
    BT_CHECK(nr_free_pages() == before, "free count not restored");
    buddy_pmm_manager.check();
    bt_case_pass();
}

// entry points, called from pmm_init() in the UCORE_BUDDY_TEST build only

void
buddy_run_fixture_tests(void) {
    cprintf("[Buddy Test] fixture tests on a controlled %d-page zone\n", BT_NR_PAGES);
    BT_CHECK(pmm_manager == &buddy_pmm_manager, "fixture tests need the buddy manager");
    BT_CHECK(bt_free() == 0, "manager must be empty before real memory is registered");
    bt_saved_pages = pages;
    bt_saved_npage = npage;
    pages = bt_pages;
    npage = nbase + BT_NR_PAGES;

    bt_case_init_full();
    bt_case_init_offset();
    bt_case_init_single();
    bt_case_rounding();
    bt_case_failure_invariance();
    bt_case_split_merge_orders();
    bt_case_split_merge_singles();
    bt_case_adjacent_not_buddy();
    bt_case_buddy_pair();
    bt_case_fragmentation();

    // hand the real layout back: page_init() needs the original pages/npage
    // and an empty manager
    pages = bt_saved_pages;
    npage = bt_saved_npage;
    buddy_pmm_manager.init();
    BT_CHECK(bt_free() == 0, "manager must be empty after the fixture tests");
    cprintf("[Buddy Test] fixture tests done, real page array restored\n");
}

void
buddy_run_live_tests(void) {
    cprintf("[Buddy Test] live tests on the real free pages\n");
    BT_CHECK(pmm_manager == &buddy_pmm_manager, "live tests need the buddy manager");
    bt_case_live_alloc();
    bt_case_live_reuse();
    cprintf("[Buddy Test] ALL TESTS PASSED (%d cases)\n", bt_case_count);
}

#endif /* UCORE_BUDDY_TEST */
