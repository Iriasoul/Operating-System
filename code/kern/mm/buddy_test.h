#ifndef __KERN_MM_BUDDY_TEST_H__
#define __KERN_MM_BUDDY_TEST_H__

#include <pmm.h>

// Behavior tests for the buddy system page allocator. Both entry points are
// called from pmm_init() and only exist in the PMM=buddy BUDDY_TEST=1 build
// (-D UCORE_BUDDY_TEST, see Makefile); in every other configuration the test
// code is compiled out and these calls disappear.

// controlled-descriptor tests: run right after init_pmm_manager(), while the
// manager is initialized but real memory is not yet registered. The tests
// swap in a private static Page array, and on return the global pages/npage
// are restored and the manager is left empty, so the real page_init() can
// proceed unchanged.
void buddy_run_fixture_tests(void);

// real-memory tests: run after page_init() and the manager's own structural
// check, allocating, writing, verifying and freeing real pages through the
// public alloc_pages/free_pages/nr_free_pages wrappers.
void buddy_run_live_tests(void);

#endif /* !__KERN_MM_BUDDY_TEST_H__ */
