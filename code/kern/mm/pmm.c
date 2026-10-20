#include <default_pmm.h>
#include <best_fit_pmm.h>
#include <buddy_pmm.h>
#ifdef UCORE_BUDDY_TEST
#include <buddy_test.h>
#endif
#ifdef UCORE_SLUB
#include <slub.h>
#endif
#ifdef UCORE_SLUB_TEST
#include <slub_test.h>
#endif
#include <defs.h>
#include <error.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#include <sbi.h>
#include <stdio.h>
#include <string.h>
#include <riscv.h>
#include <dtb.h>

// virtual address of physical page array
struct Page *pages;
// amount of physical memory (in pages)
size_t npage = 0;
// the kernel image is mapped at VA=KERNBASE and PA=info.base
uint64_t va_pa_offset;
// memory starts at 0x80000000 in RISC-V
// DRAM_BASE defined in riscv.h as 0x80000000
const size_t nbase = DRAM_BASE / PGSIZE;

// virtual address of boot-time page directory
uintptr_t *satp_virtual = NULL;
// physical address of boot-time page directory
uintptr_t satp_physical;

// physical memory management
const struct pmm_manager *pmm_manager;


static void check_alloc_page(void);

// init_pmm_manager - initialize a pmm_manager instance
static void init_pmm_manager(void) {
    // the official tests require Best-Fit, so it stays the default build;
    // PMM=buddy passes -D UCORE_PMM_BUDDY (see Makefile)
#if defined(UCORE_PMM_BUDDY)
    pmm_manager = &buddy_pmm_manager;
#else
    pmm_manager = &best_fit_pmm_manager;
#endif
    cprintf("memory management: %s\n", pmm_manager->name);
    pmm_manager->init();
}

// init_memmap - call pmm->init_memmap to build Page struct for free memory
static void init_memmap(struct Page *base, size_t n) {
    pmm_manager->init_memmap(base, n);
}

// alloc_pages - call pmm->alloc_pages to allocate a continuous n*PAGESIZE
// memory
struct Page *alloc_pages(size_t n) {
    return pmm_manager->alloc_pages(n);
}

// free_pages - call pmm->free_pages to free a continuous n*PAGESIZE memory
void free_pages(struct Page *base, size_t n) {
    pmm_manager->free_pages(base, n);
}

// nr_free_pages - call pmm->nr_free_pages to get the size (nr*PAGESIZE)
// of current free memory
size_t nr_free_pages(void) {
    return pmm_manager->nr_free_pages();
}

//lab2 function1 YOUR CODE
/*
 * page_init - turn the physical memory reported by the device tree into Page
 * descriptors, keep the pages already occupied by the system reserved, and
 * hand the remaining whole pages to the current pmm manager.
 *
 * Physical layout after this function (default 128MiB):
 *   [DRAM_BASE, KERNEL_BEGIN_PADDR)             firmware (OpenSBI)
 *   [KERNEL_BEGIN_PADDR, end)                   kernel image
 *   [freemem_pa, freemem_pa + (npage - nbase) * sizeof(struct Page))
 *                                               Page descriptor array
 *   [free_start_pa, npage << PGSHIFT)           free pages for the manager
 */
static void
page_init(void) {
    // the boot mapping translates KERNBASE.. linearly to DRAM_BASE.., and
    // every PADDR/pa2page call below depends on this offset
    va_pa_offset = PHYSICAL_MEMORY_OFFSET;

    uint64_t mem_base = get_memory_base();
    uint64_t mem_size = get_memory_size();
    cprintf("page_init: detected physical memory base 0x%016lx, size 0x%016lx\n",
            mem_base, mem_size);

    // this framework only models one DRAM region starting at DRAM_BASE
    if (mem_base != DRAM_BASE) {
        panic("page_init: unsupported memory base 0x%016lx", mem_base);
    }
    if (mem_size == 0) {
        panic("page_init: zero-length physical memory");
    }
    uint64_t mem_end = mem_base + mem_size;
    if (mem_end < mem_base) {
        panic("page_init: memory range overflow");
    }
    if (mem_end > PHYSICAL_MEMORY_END) {
        mem_end = PHYSICAL_MEMORY_END;
    }

    // a partial trailing page is not manageable
    npage = PPN(mem_end);
    if (npage <= nbase) {
        panic("page_init: no manageable page (npage = %lu)", (unsigned long)npage);
    }

    // npage is bounded by PPN(PHYSICAL_MEMORY_END), so the descriptor count
    // and size computations below cannot overflow
    size_t nr_desc = npage - nbase;

    extern char end[];  // virtual address of the end of the kernel image
    uintptr_t freemem_va = ROUNDUP((uintptr_t)end, PGSIZE);
    uintptr_t freemem_pa = PADDR(freemem_va);
    uintptr_t free_start_va = ROUNDUP(freemem_va + nr_desc * sizeof(struct Page), PGSIZE);
    uintptr_t free_start_pa = PADDR(free_start_va);
    size_t free_start_ppn = PPN(free_start_pa);

    // validate the layout before writing anything into the array area
    if (PPN(freemem_pa) >= npage) {
        panic("page_init: kernel image out of managed memory");
    }
    if (free_start_ppn > npage) {
        panic("page_init: no room for %lu page descriptors", (unsigned long)nr_desc);
    }

    pages = (struct Page *)freemem_va;
    for (size_t i = 0; i < nr_desc; i ++) {
        struct Page *p = pages + i;
        p->flags = p->property = 0;
        set_page_ref(p, 0);
        SetPageReserved(p);
    }

    size_t nfree = npage - free_start_ppn;
    cprintf("page_init: manage [0x%016lx, 0x%016lx), %lu pages, descriptors at 0x%016lx\n",
            mem_base, mem_end, (unsigned long)nr_desc, freemem_va);
    if (nfree > 0) {
        cprintf("page_init: free pages [0x%016lx, 0x%016lx), %lu pages\n",
                free_start_pa, (uintptr_t)npage << PGSHIFT, (unsigned long)nfree);
        // the First-Fit manager requires every registered page to be
        // Reserved before init_memmap
        init_memmap(pa2page(free_start_pa), nfree);
    } else {
        cprintf("page_init: no whole free page left for the pmm manager\n");
    }
}

/* pmm_init - initialize the physical memory management */
void pmm_init(void) {
    // We need to alloc/free the physical memory (granularity is 4KB or other size).
    // So a framework of physical memory manager (struct pmm_manager)is defined in pmm.h
    // First we should init a physical memory manager(pmm) based on the framework.
    // Then pmm can alloc/free the physical memory.
    // Now the first_fit/best_fit/worst_fit/buddy_system pmm are available.
    init_pmm_manager();

#ifdef UCORE_BUDDY_TEST
    // controlled-descriptor buddy behavior tests: run while the manager is
    // initialized but real memory is not yet registered; they swap in a
    // private Page array and restore the globals afterwards
    buddy_run_fixture_tests();
#endif

    // detect physical memory space, reserve already used memory,
    // then use pmm->init_memmap to create free page list
    page_init();

    // use pmm->check to verify the correctness of the alloc/free function in a pmm
    check_alloc_page();

#ifdef UCORE_BUDDY_TEST
    // real-memory buddy behavior tests, after the manager's structural check
    buddy_run_live_tests();
#endif

#ifdef UCORE_SLUB
    // object allocator on top of the buddy manager: legal only after the
    // real pages are registered (page_init) and the manager has been checked
    slub_init();
#endif

#ifdef UCORE_SLUB_TEST
    // dedicated SLUB behavior tests: run on the freshly initialized, empty
    // allocator; they hand every page back before the boot continues
    slub_run_tests();
#endif

    extern char boot_page_table_sv39[];
    satp_virtual = (pte_t*)boot_page_table_sv39;
    satp_physical = PADDR(satp_virtual);
    cprintf("satp virtual address: 0x%016lx\nsatp physical address: 0x%016lx\n", satp_virtual, satp_physical);
}

static void check_alloc_page(void) {
    pmm_manager->check();
    cprintf("check_alloc_page() succeeded!\n");
}
