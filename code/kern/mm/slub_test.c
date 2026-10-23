#include <defs.h>
#include <memlayout.h>
#include <pmm.h>
#include <stdio.h>
#include <slub.h>
#include <slub_test.h>

#ifdef UCORE_SLUB_TEST

/*
 * Behavior tests for the simplified SLUB object allocator (kmalloc/kfree).
 *
 * Build: PMM=buddy SLUB=1 SLUB_TEST=1 (passes -D UCORE_SLUB_TEST, see
 * Makefile); in every other configuration this file compiles to nothing and
 * pmm.c does not call into it. pmm_init() invokes slub_run_tests() right
 * after slub_init(), so the tests start on a freshly initialized allocator
 * holding no objects and no slabs, and every case must hand back every page
 * it took before the next one starts.
 *
 * The tests drive the allocator only through its public interface (slub.h:
 * kmalloc/kfree/slub_check) plus the public page wrappers alloc_pages/
 * free_pages/nr_free_pages. No private structure of slub.c is replicated
 * and slub.c is not included. Expectations come in two families:
 *
 *   B-cases - the behavior contract only: kmalloc(0)/kfree(NULL), 16-byte
 *     alignment, the requested size bytes are private to one object,
 *     failures leave the state unchanged, pages live exactly as long as
 *     their last object, real underlying exhaustion is survivable and fully
 *     recoverable. No capacity or header number enters these cases.
 *   L-cases - layout acceptance of the current implementation, using the
 *     published facts only: 48-byte in-page headers, per-class capacities
 *     253/126/63/31/15/7/3/1, a slot stride equal to the class size, and
 *     the ceil((size+48)/4096)->power-of-two occupancy of large blocks, and
 *     full -> partial -> full slot reuse at the published capacity. If
 *     the layout legitimately changes, these numbers change with it; they
 *     are kept in their own cases so the contract cases stay valid.
 *
 * Every case prints its own PASS line. A violated expectation prints a
 * "[SLUB Test] FAIL" line and panics, so the final "[SLUB Test] ALL TESTS
 * PASSED" marker can only appear after every case (including the closing
 * whole-allocator slub_check) succeeded.
 *
 * Deliberately not tested: double free, freeing interior pointers, freeing
 * wild pointers, and concurrent or interrupt-context use - none of these is
 * promised by the contract. Returned memory is never assumed zeroed; every
 * object is written before it is first read.
 */

#define ST_HOLD_MAX 32768   /* page descriptor count of the current framework */

#define ST_FAIL(fmt, ...)                                                      \
    do {                                                                       \
        cprintf("[SLUB Test] FAIL: %s:%d in %s: " fmt "\n",                    \
                __FILE__, __LINE__, __func__, ##__VA_ARGS__);                  \
        panic("slub_test aborted");                                            \
    } while (0)

#define ST_CHECK(cond, fmt, ...)                                               \
    do {                                                                       \
        if (!(cond)) {                                                         \
            ST_FAIL("check `" #cond "`: " fmt, ##__VA_ARGS__);                 \
        }                                                                      \
    } while (0)

static int st_case_count;
static size_t st_initial;               /* free pages before the first case */

/* exhaustion bookkeeping: every drained page, freed with original request 1 */
static struct Page *st_hold[ST_HOLD_MAX];
static size_t st_held;

/* object tables of the layout cases (the 16-byte class fills 254 slots) */
static void *st_objs[260];

static void
st_case_begin(const char *name) {
    st_case_count ++;
    cprintf("[SLUB Test] case %d: %s\n", st_case_count, name);
    ST_CHECK(nr_free_pages() == st_initial,
             "case starts with %lu free pages instead of %lu: an earlier case leaked",
             (unsigned long)nr_free_pages(), (unsigned long)st_initial);
}

static void
st_case_pass(void) {
    cprintf("[SLUB Test] case %d PASS\n", st_case_count);
}

/* byte-exact pattern over exactly the requested size bytes */
static void
st_fill(void *p, size_t n, unsigned char tag) {
    unsigned char *b = (unsigned char *)p;
    size_t i;
    for (i = 0; i < n; i ++) {
        b[i] = (unsigned char)(tag ^ i);
    }
}

static void
st_verify(const void *p, size_t n, unsigned char tag) {
    const unsigned char *b = (const unsigned char *)p;
    size_t i;
    for (i = 0; i < n; i ++) {
        ST_CHECK(b[i] == (unsigned char)(tag ^ i),
                 "byte %lu of the object tagged 0x%02x was clobbered",
                 (unsigned long)i, tag);
    }
}

static int
st_overlap(uintptr_t a, size_t na, uintptr_t b, size_t nb) {
    return a < b + nb && b < a + na;
}

// ---- B: behavior contract --------------------------------------------------

// kmalloc(0) and kfree(NULL) change nothing; oversized requests fail
// cleanly and leave a working allocator behind
static void
st_case_boundary(void) {
    st_case_begin("boundary: kmalloc(0), kfree(NULL), oversized requests");
    size_t before = nr_free_pages();
    ST_CHECK(kmalloc(0) == NULL, "kmalloc(0) must return NULL");
    ST_CHECK(nr_free_pages() == before, "kmalloc(0) must not touch any page");
    kfree(NULL);
    ST_CHECK(nr_free_pages() == before, "kfree(NULL) must not change the state");
    ST_CHECK(kmalloc((size_t)-1) == NULL, "a (size_t)-1 request must fail");
    ST_CHECK(kmalloc(((size_t)-1) / 2) == NULL,
             "a half-address-space request must fail");
    ST_CHECK(nr_free_pages() == before, "failed requests must not touch any page");
    /* a pure NULL check could hide a wedged allocator: prove it still serves */
    void *p = kmalloc(1);
    ST_CHECK(p != NULL, "allocator unusable after failed requests");
    st_fill(p, 1, 0xA1);
    st_verify(p, 1, 0xA1);
    kfree(p);
    ST_CHECK(nr_free_pages() == before, "free count not restored");
    st_case_pass();
}

static const size_t st_align_sizes[] = {
    1, 9, 16, 17, 100, 777, 2048, 2049, 4096, 10000,
};
#define ST_NR_ALIGN (sizeof(st_align_sizes) / sizeof(st_align_sizes[0]))

// every return value of every size is at least 16-byte aligned and accepts
// a full-size write/read cycle
static void
st_case_alignment(void) {
    st_case_begin("alignment: every return value 16-byte aligned, full-size writes");
    size_t before = nr_free_pages();
    void *ptrs[ST_NR_ALIGN];
    size_t i;
    for (i = 0; i < ST_NR_ALIGN; i ++) {
        ptrs[i] = kmalloc(st_align_sizes[i]);
        ST_CHECK(ptrs[i] != NULL, "kmalloc(%lu) failed",
                 (unsigned long)st_align_sizes[i]);
        ST_CHECK((((uintptr_t)ptrs[i]) & 15) == 0,
                 "kmalloc(%lu) returned 0x%016lx, not 16-byte aligned",
                 (unsigned long)st_align_sizes[i], (unsigned long)ptrs[i]);
        st_fill(ptrs[i], st_align_sizes[i], (unsigned char)(0x30 + i));
    }
    for (i = 0; i < ST_NR_ALIGN; i ++) {
        st_verify(ptrs[i], st_align_sizes[i], (unsigned char)(0x30 + i));
    }
    for (i = ST_NR_ALIGN; i > 0; i --) {
        kfree(ptrs[i - 1]);
    }
    ST_CHECK(nr_free_pages() == before, "free count not restored");
    st_case_pass();
}

// six 17-byte objects: if the allocator packed them into 16-byte slots or
// aliased two returns, the full-size writes below collide and one of the
// range or pattern checks fires
static void
st_case_isolation(void) {
    st_case_begin("isolation: six 17-byte objects stay private");
    size_t before = nr_free_pages();
    void *obj[6];
    size_t i, k;
    for (i = 0; i < 6; i ++) {
        obj[i] = kmalloc(17);
        ST_CHECK(obj[i] != NULL, "object %lu allocation failed", (unsigned long)i);
        for (k = 0; k < i; k ++) {
            ST_CHECK(!st_overlap((uintptr_t)obj[i], 17, (uintptr_t)obj[k], 17),
                     "object %lu overlaps object %lu",
                     (unsigned long)i, (unsigned long)k);
        }
        st_fill(obj[i], 17, (unsigned char)(0x40 + i));
        /* every earlier object must have survived this fill */
        for (k = 0; k < i; k ++) {
            st_verify(obj[k], 17, (unsigned char)(0x40 + k));
        }
    }
    /* a free in the middle, then a fresh object of the same size */
    kfree(obj[2]);
    void *fresh = kmalloc(17);
    ST_CHECK(fresh != NULL, "allocation after a middle free failed");
    for (i = 0; i < 6; i ++) {
        if (i != 2) {
            ST_CHECK(!st_overlap((uintptr_t)fresh, 17, (uintptr_t)obj[i], 17),
                     "the fresh object overlaps live object %lu", (unsigned long)i);
        }
    }
    st_fill(fresh, 17, 0x60);
    for (i = 0; i < 6; i ++) {
        if (i != 2) {
            st_verify(obj[i], 17, (unsigned char)(0x40 + i));
        }
    }
    st_verify(fresh, 17, 0x60);
    /* hand every live allocation back, in an order unlike the allocations */
    kfree(obj[5]);
    kfree(fresh);
    kfree(obj[0]);
    kfree(obj[4]);
    kfree(obj[1]);
    kfree(obj[3]);
    ST_CHECK(nr_free_pages() == before, "free count not restored");
    st_case_pass();
}

// a page lives exactly as long as its last object: gone while the object
// lives, back the moment the object is freed, never kept cached
static void
st_case_page_hold(void) {
    st_case_begin("page hold: pages live exactly as long as their objects");
    size_t before = nr_free_pages();
    /* a slab is exactly one page, so a single fresh 2048-byte object holds
       exactly one page, whatever the per-page capacity of its class is */
    void *x = kmalloc(2048);
    ST_CHECK(x != NULL, "kmalloc(2048) failed");
    ST_CHECK(nr_free_pages() == before - 1,
             "one small object must hold exactly one page");
    st_fill(x, 2048, 0x70);
    st_verify(x, 2048, 0x70);
    kfree(x);
    ST_CHECK(nr_free_pages() == before,
             "the page must return to the buddy allocator immediately "
             "after the last object is freed");
    /* two 16-byte objects: the shared page must outlive each single free */
    void *a = kmalloc(16);
    void *b = kmalloc(16);
    ST_CHECK(a != NULL && b != NULL, "16-byte allocations failed");
    ST_CHECK(nr_free_pages() < before, "live objects must hold their page");
    st_fill(a, 16, 0x71);
    st_fill(b, 16, 0x72);
    st_verify(a, 16, 0x71);
    st_verify(b, 16, 0x72);
    kfree(a);
    ST_CHECK(nr_free_pages() < before,
             "the page must be kept while another object still lives");
    kfree(b);
    ST_CHECK(nr_free_pages() == before,
             "all pages must return after the last object is freed");
    st_case_pass();
}

static const size_t st_large_sizes[4] = {2049, 4096, 8193, 10000};

// large objects coexist, tolerate full-size writes, survive each other's
// free and leave no page behind
static void
st_case_large(void) {
    st_case_begin("large: four sizes live together, scrambled free");
    size_t before = nr_free_pages();
    void *lp[4];
    size_t i;
    for (i = 0; i < 4; i ++) {
        lp[i] = kmalloc(st_large_sizes[i]);
        ST_CHECK(lp[i] != NULL, "kmalloc(%lu) failed",
                 (unsigned long)st_large_sizes[i]);
        ST_CHECK((((uintptr_t)lp[i]) & 15) == 0,
                 "large object %lu not 16-byte aligned", (unsigned long)i);
        st_fill(lp[i], st_large_sizes[i], (unsigned char)(0x80 + i));
    }
    /* a small object allocated between the large ones */
    void *s = kmalloc(16);
    ST_CHECK(s != NULL, "small allocation next to large objects failed");
    st_fill(s, 16, 0x90);
    for (i = 0; i < 4; i ++) {
        st_verify(lp[i], st_large_sizes[i], (unsigned char)(0x80 + i));
    }
    /* free in an order unlike the allocations; survivors stay intact */
    kfree(lp[1]);
    st_verify(lp[0], st_large_sizes[0], 0x80);
    st_verify(lp[2], st_large_sizes[2], 0x82);
    st_verify(lp[3], st_large_sizes[3], 0x83);
    st_verify(s, 16, 0x90);
    kfree(lp[3]);
    st_verify(lp[0], st_large_sizes[0], 0x80);
    st_verify(lp[2], st_large_sizes[2], 0x82);
    kfree(lp[0]);
    st_verify(lp[2], st_large_sizes[2], 0x82);
    st_verify(s, 16, 0x90);
    kfree(lp[2]);
    kfree(s);
    ST_CHECK(nr_free_pages() == before, "free count not restored");
    st_case_pass();
}

// small and large interleaved, with the structural slub_check run once on
// a fully populated allocator and once on the emptied one
static void
st_case_mixed(void) {
    st_case_begin("mixed: small and large interleaved, slub_check in between");
    size_t before = nr_free_pages();
    void *s1 = kmalloc(16);
    void *l1 = kmalloc(2049);
    void *s2 = kmalloc(512);
    void *l2 = kmalloc(8193);
    void *s3 = kmalloc(2048);
    ST_CHECK(s1 != NULL && l1 != NULL && s2 != NULL && l2 != NULL && s3 != NULL,
             "mixed allocations failed");
    st_fill(s1, 16, 0xA0);
    st_fill(l1, 2049, 0xA1);
    st_fill(s2, 512, 0xA2);
    st_fill(l2, 8193, 0xA3);
    st_fill(s3, 2048, 0xA4);
    st_verify(s1, 16, 0xA0);
    st_verify(l1, 2049, 0xA1);
    st_verify(s2, 512, 0xA2);
    st_verify(l2, 8193, 0xA3);
    st_verify(s3, 2048, 0xA4);
    /* structural check with partial, full and large state present */
    slub_check();
    kfree(l2);
    void *s4 = kmalloc(100);
    ST_CHECK(s4 != NULL, "allocation after freeing a large object failed");
    st_fill(s4, 100, 0xA5);
    kfree(s2);
    void *s5 = kmalloc(1024);
    ST_CHECK(s5 != NULL, "allocation after freeing a small object failed");
    st_fill(s5, 1024, 0xA6);
    st_verify(s1, 16, 0xA0);
    st_verify(l1, 2049, 0xA1);
    st_verify(s3, 2048, 0xA4);
    st_verify(s4, 100, 0xA5);
    st_verify(s5, 1024, 0xA6);
    kfree(s3);
    kfree(s1);
    kfree(s5);
    kfree(l1);
    kfree(s4);
    ST_CHECK(nr_free_pages() == before, "free count not restored");
    /* and with everything back in the buddy allocator */
    slub_check();
    st_case_pass();
}

/* take single pages until only `target` free pages remain; every returned
   descriptor is remembered so it can be handed back with its original
   request 1 */
static void
st_drain_to(size_t target) {
    while (nr_free_pages() > target) {
        ST_CHECK(st_held < ST_HOLD_MAX, "exhaustion bookkeeping overflow");
        struct Page *p = alloc_pages(1);
        ST_CHECK(p != NULL, "single-page request failed with %lu pages claimed free",
                 (unsigned long)nr_free_pages());
        st_hold[st_held ++] = p;
    }
    ST_CHECK(nr_free_pages() == target, "drain left %lu free pages",
             (unsigned long)nr_free_pages());
}

// real underlying exhaustion manufactured through alloc_pages(1): every
// kmalloc path must report failure cleanly, and after the pages return in
// an arbitrary order the allocator must work again without any leak
static void
st_case_exhaustion(void) {
    st_case_begin("exhaustion: real drain, clean failures, full recovery");
    size_t before = nr_free_pages();
    st_held = 0;
    /* 1. leave only three free pages: no arrangement of three pages can
       satisfy a request that needs at least four, whatever the header size */
    st_drain_to(3);
    ST_CHECK(kmalloc(12288) == NULL,
             "12288 bytes can never fit in three pages, must fail");
    ST_CHECK(nr_free_pages() == 3, "the failed large request changed the free count");
    void *one = kmalloc(2049);
    ST_CHECK(one != NULL, "a one-page-backed object must still be served");
    st_fill(one, 2049, 0xB0);
    st_verify(one, 2049, 0xB0);
    kfree(one);
    ST_CHECK(nr_free_pages() == 3, "free count not restored after the small object");
    /* 2. take the last three pages: every allocation path must now fail */
    st_drain_to(0);
    ST_CHECK(kmalloc(16) == NULL, "small path must fail when no page is left");
    ST_CHECK(kmalloc(2048) == NULL, "2048-byte path must fail when no page is left");
    ST_CHECK(kmalloc(2049) == NULL, "large path must fail when no page is left");
    ST_CHECK(kmalloc(12288) == NULL, "large path must fail when no page is left");
    ST_CHECK(kmalloc(0) == NULL, "kmalloc(0) still fails");
    kfree(NULL);
    ST_CHECK(nr_free_pages() == 0, "failed requests must not touch any page");
    /* 3. hand every page back in an order unlike the allocation order: the
       buddy allocator must reassemble everything and SLUB must work again */
    size_t i;
    for (i = st_held; i > 0; i --) {
        size_t count = nr_free_pages();
        free_pages(st_hold[i - 1], 1);
        ST_CHECK(nr_free_pages() == count + 1,
                 "freeing page %lu changed the count by more than 1",
                 (unsigned long)(i - 1));
    }
    ST_CHECK(nr_free_pages() == before, "drain did not fully recover");
    void *p = kmalloc(32);
    ST_CHECK(p != NULL, "allocator unusable after full recovery");
    st_fill(p, 32, 0xB1);
    st_verify(p, 32, 0xB1);
    kfree(p);
    ST_CHECK(nr_free_pages() == before, "free count not restored");
    st_case_pass();
}

// ---- L: layout acceptance ---------------------------------------------------

/* published layout facts of the current implementation: class sizes, the
   number of slots one page holds per class, and the 48-byte in-page header
   that the first slot starts behind */
static const size_t st_class_size[8] = {16, 32, 64, 128, 256, 512, 1024, 2048};
static const size_t st_class_cap[8]  = {253, 126, 63, 31, 15, 7, 3, 1};

static void
st_case_layout_capacity(void) {
    st_case_begin("layout capacity: 253/126/63/31/15/7/3/1 slots per page");
    size_t i, k;
    for (i = 0; i < 8; i ++) {
        size_t before = nr_free_pages();
        size_t cap = st_class_cap[i];
        for (k = 0; k < cap; k ++) {
            st_objs[k] = kmalloc(st_class_size[i]);
            ST_CHECK(st_objs[k] != NULL, "class %lu: object %lu failed",
                     (unsigned long)i, (unsigned long)k);
            if (k == 0) {
                ST_CHECK((((uintptr_t)st_objs[0]) & (PGSIZE - 1)) == 48,
                         "class %lu: first slot not at in-page offset 48",
                         (unsigned long)i);
            }
            st_fill(st_objs[k], st_class_size[i], (unsigned char)(0xC0 + k));
        }
        ST_CHECK(nr_free_pages() == before - 1,
                 "class %lu: %lu objects must share exactly one page",
                 (unsigned long)i, (unsigned long)cap);
        for (k = 0; k < cap; k ++) {
            st_verify(st_objs[k], st_class_size[i], (unsigned char)(0xC0 + k));
        }
        /* one object more than the capacity must force a second page */
        void *extra = kmalloc(st_class_size[i]);
        ST_CHECK(extra != NULL, "class %lu: object %lu failed",
                 (unsigned long)i, (unsigned long)cap);
        ST_CHECK(nr_free_pages() == before - 2,
                 "class %lu: capacity must be exactly %lu per page",
                 (unsigned long)i, (unsigned long)cap);
        kfree(extra);
        for (k = cap; k > 0; k --) {
            kfree(st_objs[k - 1]);
        }
        ST_CHECK(nr_free_pages() == before, "class %lu: free count not restored",
                 (unsigned long)i);
    }
    st_case_pass();
}

/* boundary sizes and the slot stride the minimal fitting class produces:
   the stride equals the class size (published layout) */
static const struct {
    size_t req, stride;
} st_stride[] = {
    {1, 16}, {16, 16}, {17, 32}, {32, 32}, {33, 64}, {64, 64},
    {65, 128}, {128, 128}, {129, 256}, {256, 256}, {257, 512},
    {512, 512}, {513, 1024}, {1024, 1024},
};
#define ST_NR_STRIDE (sizeof(st_stride) / sizeof(st_stride[0]))

static void
st_case_layout_stride(void) {
    st_case_begin("layout stride: boundary sizes pick the minimal class");
    size_t i;
    for (i = 0; i < ST_NR_STRIDE; i ++) {
        size_t before = nr_free_pages();
        void *a = kmalloc(st_stride[i].req);
        void *b = kmalloc(st_stride[i].req);
        ST_CHECK(a != NULL && b != NULL, "request %lu failed",
                 (unsigned long)st_stride[i].req);
        ST_CHECK(nr_free_pages() == before - 1,
                 "request %lu: both objects must share one fresh slab page",
                 (unsigned long)st_stride[i].req);
        /* both come from the fresh slab, lowest slots first */
        ST_CHECK((uintptr_t)b - (uintptr_t)a == st_stride[i].stride,
                 "request %lu: slot distance %lu, expected stride %lu",
                 (unsigned long)st_stride[i].req,
                 (unsigned long)((uintptr_t)b - (uintptr_t)a),
                 (unsigned long)st_stride[i].stride);
        ST_CHECK((((uintptr_t)a) & (PGSIZE - 1)) == 48,
                 "request %lu: first slot not at in-page offset 48",
                 (unsigned long)st_stride[i].req);
        st_fill(a, st_stride[i].req, 0xD0);
        st_fill(b, st_stride[i].req, 0xD1);
        st_verify(a, st_stride[i].req, 0xD0);
        st_verify(b, st_stride[i].req, 0xD1);
        kfree(b);
        kfree(a);
        ST_CHECK(nr_free_pages() == before, "request %lu: free count not restored",
                 (unsigned long)st_stride[i].req);
    }
    /* freeing the most recent slot of a page hands that exact slot out
       again without taking a new page */
    size_t before = nr_free_pages();
    void *a = kmalloc(64);
    void *b = kmalloc(64);
    ST_CHECK(a != NULL && b != NULL, "64-byte allocations failed");
    kfree(a);
    void *c = kmalloc(64);
    ST_CHECK(c != NULL && nr_free_pages() == before - 1,
             "the freed slot must be reused without a new page");
    ST_CHECK(c == a, "the freed slot was not handed out again");
    kfree(c);
    kfree(b);
    ST_CHECK(nr_free_pages() == before, "free count not restored");
    st_case_pass();
}

/* published large-object layout: the payload starts at in-page offset 48;
   the recorded page request is ceil((size+48)/4096) and the buddy allocator
   occupies the next power of two. 4048 vs 4049 pins the header at exactly
   48 (4048 still fits one page, 4049 does not); 8193/16385/65536 pin the
   power-of-two occupancy. Occupancy written out explicitly. */
static const struct {
    size_t req, occ;
} st_large_layout[] = {
    {2049, 1}, {4048, 1}, {4049, 2}, {8193, 4}, {16385, 8}, {65536, 32},
};
#define ST_NR_LARGELAYOUT (sizeof(st_large_layout) / sizeof(st_large_layout[0]))

static void
st_case_layout_large(void) {
    st_case_begin("layout large: header 48, occupancy ceil->power of two");
    size_t start = nr_free_pages();
    void *lp[ST_NR_LARGELAYOUT];
    size_t i;
    for (i = 0; i < ST_NR_LARGELAYOUT; i ++) {
        /* the earlier objects of this case stay live, so the expected count
           is taken fresh per allocation */
        size_t before = nr_free_pages();
        lp[i] = kmalloc(st_large_layout[i].req);
        ST_CHECK(lp[i] != NULL, "kmalloc(%lu) failed",
                 (unsigned long)st_large_layout[i].req);
        ST_CHECK(nr_free_pages() == before - st_large_layout[i].occ,
                 "kmalloc(%lu) occupied %lu free pages, expected %lu",
                 (unsigned long)st_large_layout[i].req,
                 (unsigned long)(before - nr_free_pages()),
                 (unsigned long)st_large_layout[i].occ);
        ST_CHECK((((uintptr_t)lp[i]) & (PGSIZE - 1)) == 48,
                 "kmalloc(%lu): payload not at in-page offset 48",
                 (unsigned long)st_large_layout[i].req);
        st_fill(lp[i], st_large_layout[i].req, (unsigned char)(0xE0 + i));
    }
    for (i = 0; i < ST_NR_LARGELAYOUT; i ++) {
        st_verify(lp[i], st_large_layout[i].req, (unsigned char)(0xE0 + i));
    }
    /* every free must restore exactly its occupied size, whatever the
       order, and the survivors must stay intact */
    static const size_t order[ST_NR_LARGELAYOUT] = {3, 0, 5, 2, 4, 1};
    for (i = 0; i < ST_NR_LARGELAYOUT; i ++) {
        size_t k = order[i];
        size_t count = nr_free_pages();
        kfree(lp[k]);
        ST_CHECK(nr_free_pages() == count + st_large_layout[k].occ,
                 "freeing %lu bytes restored %lu pages, expected %lu",
                 (unsigned long)st_large_layout[k].req,
                 (unsigned long)(nr_free_pages() - count),
                 (unsigned long)st_large_layout[k].occ);
        size_t j;
        for (j = i + 1; j < ST_NR_LARGELAYOUT; j ++) {
            size_t m = order[j];
            st_verify(lp[m], st_large_layout[m].req, (unsigned char)(0xE0 + m));
        }
    }
    ST_CHECK(nr_free_pages() == start, "free count not restored");
    st_case_pass();
}

/* The published 1024-byte class holds three objects per page. Unlike the
   partial-page reuse in the stride case, this exercises both transitions
   out of and back into the full list while two objects keep the page live. */
static void
st_case_layout_full_reuse(void) {
    st_case_begin("layout full reuse: 1024-byte slab full -> partial -> full");
    size_t before = nr_free_pages();
    void *obj[3];
    size_t i, j;
    for (i = 0; i < 3; i ++) {
        obj[i] = kmalloc(1024);
        ST_CHECK(obj[i] != NULL, "1024-byte object %lu failed", (unsigned long)i);
        ST_CHECK((((uintptr_t)obj[i]) & 15) == 0,
                 "object %lu is not 16-byte aligned", (unsigned long)i);
        ST_CHECK(((uintptr_t)obj[i] / PGSIZE) == ((uintptr_t)obj[0] / PGSIZE),
                 "three 1024-byte objects must share one slab page");
        for (j = 0; j < i; j ++) {
            ST_CHECK(!st_overlap((uintptr_t)obj[i], 1024, (uintptr_t)obj[j], 1024),
                     "objects %lu and %lu overlap", (unsigned long)i, (unsigned long)j);
        }
        st_fill(obj[i], 1024, (unsigned char)(0xF0 + i));
    }
    ST_CHECK(nr_free_pages() == before - 1, "full slab must occupy exactly one page");
    for (i = 0; i < 3; i ++) {
        st_verify(obj[i], 1024, (unsigned char)(0xF0 + i));
    }
    slub_check();

    uintptr_t released = (uintptr_t)obj[1];
    kfree(obj[1]);
    obj[1] = NULL;
    ST_CHECK(nr_free_pages() == before - 1,
             "freeing one slot must retain the page with two live objects");
    st_verify(obj[0], 1024, 0xF0);
    st_verify(obj[2], 1024, 0xF2);
    slub_check();

    obj[1] = kmalloc(1024);
    ST_CHECK(obj[1] != NULL, "allocation from the formerly full slab failed");
    ST_CHECK((uintptr_t)obj[1] == released, "the sole freed slot was not reused");
    ST_CHECK(nr_free_pages() == before - 1, "slot reuse must not allocate another page");
    st_fill(obj[1], 1024, 0xF3);
    st_verify(obj[0], 1024, 0xF0);
    st_verify(obj[1], 1024, 0xF3);
    st_verify(obj[2], 1024, 0xF2);
    slub_check();
    cprintf("[SLUB Test] full reuse: same slot, one page held, live data intact\n");

    kfree(obj[2]);
    kfree(obj[0]);
    ST_CHECK(nr_free_pages() == before - 1, "last live object must retain its page");
    st_verify(obj[1], 1024, 0xF3);
    kfree(obj[1]);
    ST_CHECK(nr_free_pages() == before, "full-reuse case did not restore all pages");
    slub_check();
    st_case_pass();
}

// entry point, called from pmm_init() in the UCORE_SLUB_TEST build only

void
slub_run_tests(void) {
    cprintf("[SLUB Test] behavior tests on the freshly initialized allocator\n");
    st_initial = nr_free_pages();
    st_case_boundary();
    st_case_alignment();
    st_case_isolation();
    st_case_page_hold();
    st_case_large();
    st_case_mixed();
    st_case_exhaustion();
    st_case_layout_capacity();
    st_case_layout_stride();
    st_case_layout_large();
    st_case_layout_full_reuse();
    /* closing case: empty allocator, structurally consistent */
    st_case_begin("final: empty allocator, structurally consistent");
    slub_check();
    st_case_pass();
    cprintf("[SLUB Test] ALL TESTS PASSED (%d cases)\n", st_case_count);
}

#endif /* UCORE_SLUB_TEST */
