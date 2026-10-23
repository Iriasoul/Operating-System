/* Copied into a temporary kernel by check_best_fit_tail.py. */
#include <best_fit_pmm.h>
#include <pmm.h>
#include <stdio.h>

#define TAIL_PAGES 16
static struct Page tail_pages[TAIL_PAGES];
static struct Page *saved_pages;
static size_t saved_npage;

/* Run before real page_init(), so no live allocator state is discarded. */
static void tail_begin(void) {
    assert(pmm_manager == &best_fit_pmm_manager);
    assert(nr_free_pages() == 0);
    saved_pages = pages;
    saved_npage = npage;
    pages = tail_pages;
    npage = nbase + TAIL_PAGES;
    pmm_manager->init();
    for (size_t i = 0; i < TAIL_PAGES; i++) {
        tail_pages[i].flags = tail_pages[i].property = 0;
        set_page_ref(tail_pages + i, 0);
        SetPageReserved(tail_pages + i);
    }
}

static void tail_end(void) {
    pmm_manager->init();
    pages = saved_pages;
    npage = saved_npage;
}

static void test_init_tail(void) {
    cprintf("[Best-Fit Tail Test] init_memmap tail insertion\n");
    tail_begin();
    pmm_manager->init_memmap(tail_pages, 2);
    pmm_manager->init_memmap(tail_pages + 3, 1);
    assert(nr_free_pages() == 3);

    struct Page *x = alloc_pages(1);
    struct Page *y = alloc_pages(1);
    assert(x == tail_pages + 3);
    assert(x != y);
    assert(y == tail_pages);
    struct Page *z = alloc_pages(1);
    assert(z == tail_pages + 1);
    assert(nr_free_pages() == 0 && alloc_pages(1) == NULL);

    free_pages(x, 1);
    free_pages(y, 1);
    free_pages(z, 1);
    assert(nr_free_pages() == 3);
    assert(tail_pages[0].property == 2);
    assert(tail_pages[3].page_link.prev == &tail_pages[0].page_link);
    tail_end();
    cprintf("[Best-Fit Tail Test] init_memmap tail insertion PASS\n");
}

static void test_free_tail(void) {
    cprintf("[Best-Fit Tail Test] free_pages tail insertion\n");
    tail_begin();
    pmm_manager->init_memmap(tail_pages, TAIL_PAGES);
    struct Page *a = alloc_pages(2);
    struct Page *gap = alloc_pages(1);
    struct Page *b = alloc_pages(1);
    struct Page *rest = alloc_pages(12);
    assert(a == tail_pages && gap == tail_pages + 2);
    assert(b == tail_pages + 3 && rest == tail_pages + 4);
    assert(nr_free_pages() == 0);

    free_pages(a, 2);
    free_pages(b, 1);
    assert(nr_free_pages() == 3);
    struct Page *x = alloc_pages(1);
    struct Page *y = alloc_pages(1);
    assert(x == b);
    assert(x != y);
    assert(y == a);
    assert(nr_free_pages() == 1);

    free_pages(x, 1);
    free_pages(y, 1);
    free_pages(gap, 1);
    free_pages(rest, 12);
    assert(nr_free_pages() == TAIL_PAGES);
    struct Page *whole = alloc_pages(TAIL_PAGES);
    assert(whole == tail_pages);
    assert(nr_free_pages() == 0 && alloc_pages(1) == NULL);
    free_pages(whole, TAIL_PAGES);
    assert(nr_free_pages() == TAIL_PAGES);
    tail_end();
    cprintf("[Best-Fit Tail Test] free_pages tail insertion PASS\n");
}

void best_fit_tail_run_tests(int check_init, int check_free) {
    if (check_init) {
        test_init_tail();
    }
    if (check_free) {
        test_free_tail();
    }
    cprintf("[Best-Fit Tail Test] ALL TESTS PASSED\n");
}
