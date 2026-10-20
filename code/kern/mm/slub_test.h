#ifndef __KERN_MM_SLUB_TEST_H__
#define __KERN_MM_SLUB_TEST_H__

#include <pmm.h>

// Behavior tests for the SLUB object allocator (kmalloc/kfree). The single
// entry point is called from pmm_init() right after slub_init() and only
// exists in the PMM=buddy SLUB=1 SLUB_TEST=1 build (-D UCORE_SLUB_TEST, see
// Makefile); in every other configuration the test code is compiled out and
// this call disappears. The tests start on a freshly initialized allocator
// and must hand every page they take back before they return.

void slub_run_tests(void);

#endif /* !__KERN_MM_SLUB_TEST_H__ */
