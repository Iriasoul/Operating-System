#ifndef LAB4_TEST_PMM_H
#define LAB4_TEST_PMM_H

#include "../../../kern/mm/pmm.h"

// Keep the actual types and constants; replace physical address translation.
void *test_page2kva(struct Page *page);
struct Page *test_kva2page(void *kva);
#define page2kva(page) test_page2kva(page)
#define kva2page(kva) test_kva2page(kva)

#endif
