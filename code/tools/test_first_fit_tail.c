/* Copied into a temporary kernel by check_best_fit_tail.py --manager first_fit. */
#include <default_pmm.h>
#include <pmm.h>
#include <stdio.h>

#define TAIL_PAGES 16
static struct Page tail_pages[TAIL_PAGES];
static struct Page *saved_pages;
static size_t saved_npage;

static void tail_begin(void) {
    assert(pmm_manager == &default_pmm_manager);
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
    cprintf("[First-Fit Tail Test] init_memmap tail insertion\n");
    tail_begin();
    pmm_manager->init_memmap(tail_pages, 2);
    pmm_manager->init_memmap(tail_pages + 3, 1);
    assert(nr_free_pages() == 3);
    /* Insertion must preserve both directions of the address-ordered list. */
    assert(tail_pages[0].page_link.next == &tail_pages[3].page_link);
    assert(tail_pages[3].page_link.prev == &tail_pages[0].page_link);

    struct Page *a = alloc_pages(2);
    struct Page *b = alloc_pages(1);
    assert(a == tail_pages && b == tail_pages + 3);
    assert(nr_free_pages() == 0 && alloc_pages(1) == NULL);
    free_pages(b, 1);
    free_pages(a, 2);
    assert(nr_free_pages() == 3);
    assert(tail_pages[0].property == 2 && tail_pages[3].property == 1);
    assert(tail_pages[3].page_link.prev == &tail_pages[0].page_link);
    tail_end();
    cprintf("[First-Fit Tail Test] init_memmap tail insertion PASS\n");
}

static void test_free_tail(void) {
    cprintf("[First-Fit Tail Test] free_pages tail insertion and adjacent merge\n");
    tail_begin();
    pmm_manager->init_memmap(tail_pages, TAIL_PAGES);
    struct Page *a = alloc_pages(2);
    struct Page *b = alloc_pages(1);
    struct Page *rest = alloc_pages(13);
    assert(a == tail_pages && b == tail_pages + 2 && rest == tail_pages + 3);
    assert(nr_free_pages() == 0);

    free_pages(a, 2);
    free_pages(b, 1);
    assert(nr_free_pages() == 3);
    /* Tail insertion of the adjacent page must coalesce with the low block. */
    struct Page *merged = alloc_pages(3);
    assert(merged == a);
    assert(nr_free_pages() == 0 && alloc_pages(1) == NULL);
    free_pages(merged, 3);
    free_pages(rest, 13);
    assert(nr_free_pages() == TAIL_PAGES);
    struct Page *whole = alloc_pages(TAIL_PAGES);
    assert(whole == tail_pages);
    assert(nr_free_pages() == 0 && alloc_pages(1) == NULL);
    free_pages(whole, TAIL_PAGES);
    assert(nr_free_pages() == TAIL_PAGES);
    tail_end();
    cprintf("[First-Fit Tail Test] free_pages tail insertion and adjacent merge PASS\n");
}

void first_fit_tail_run_tests(int check_init, int check_free) {
    if (check_init) {
        test_init_tail();
    }
    if (check_free) {
        test_free_tail();
    }
    cprintf("[First-Fit Tail Test] ALL TESTS PASSED\n");
}
