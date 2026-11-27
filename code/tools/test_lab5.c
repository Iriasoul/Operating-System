#include <defs.h>
#include <assert.h>
#include <error.h>
#include <proc.h>
#include <trap.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <riscv.h>
#include <pmm.h>
#include <vmm.h>

extern void exception_handler(struct trapframe *tf);

static void lab5_pass(const char *name)
{
    cprintf("[Test %s] PASS\n", name);
}

void test_lab5_process_create(void)
{
    assert(current == idleproc);
    assert(idleproc != NULL && idleproc->pid == 0);
    assert(idleproc->state == PROC_RUNNABLE);
    assert(idleproc->mm == NULL && idleproc->tf == NULL);
    assert(initproc != NULL && initproc->pid == 1);
    assert(initproc->parent == idleproc);
    assert(initproc->state == PROC_RUNNABLE);
    assert(initproc->context.ra != 0 && initproc->context.sp != 0);
    lab5_pass("Process Create");
}

void test_lab5_syscall(void)
{
    struct trapframe tf;
    struct trapframe *old = current->tf;
    memset(&tf, 0, sizeof(tf));
    tf.cause = CAUSE_USER_ECALL;
    tf.epc = 0x1000;
    tf.gpr.a0 = SYS_getpid;
    current->tf = &tf;
    exception_handler(&tf);
    assert(tf.epc == 0x1004);
    assert((int)tf.gpr.a0 == current->pid);
    tf.gpr.a0 = 0;
    exception_handler(&tf);
    assert((int)tf.gpr.a0 == -E_INVAL);
    current->tf = old;
    lab5_pass("Syscall");
}

static void test_lab5_cow(void)
{
    size_t initial_free_pages = nr_free_pages();
    struct mm_struct *parent = mm_create(), *child = mm_create();
    struct Page *parent_root = alloc_page(), *child_root = alloc_page();
    assert(parent != NULL && child != NULL && parent_root != NULL && child_root != NULL);
    parent->pgdir = page2kva(parent_root);
    child->pgdir = page2kva(child_root);
    memset(parent->pgdir, 0, PGSIZE);
    memset(child->pgdir, 0, PGSIZE);

    uintptr_t writable = USERBASE, readonly = USERBASE + 2 * PTSIZE;
    assert(mm_map(parent, writable, 2 * PGSIZE, VM_READ | VM_WRITE, NULL) == 0);
    assert(mm_map(parent, readonly, PGSIZE, VM_READ, NULL) == 0);
    struct Page *shared = pgdir_alloc_page(parent->pgdir, writable, PTE_U | PTE_R | PTE_W);
    struct Page *ro_page = pgdir_alloc_page(parent->pgdir, readonly, PTE_U | PTE_R);
    assert(shared != NULL && ro_page != NULL);
    memset(page2kva(shared), 0x5a, PGSIZE);

    assert(dup_mmap(child, parent) == 0);
    pte_t *parent_pte, *child_pte, *ro_pte;
    assert(get_page(parent->pgdir, writable, &parent_pte) == shared);
    assert(get_page(child->pgdir, writable, &child_pte) == shared);
    assert(page_ref(shared) == 2);
    assert((*parent_pte & (PTE_W | PTE_COW)) == PTE_COW);
    assert((*child_pte & (PTE_W | PTE_COW)) == PTE_COW);
    assert(get_page(child->pgdir, readonly, &ro_pte) == ro_page);
    assert((*ro_pte & (PTE_W | PTE_COW)) == 0);
    assert(do_cow_fault(child, readonly) == -E_FAULT);
    assert(do_cow_fault(child, writable + PGSIZE) == -E_FAULT);

    assert(do_cow_fault(child, writable) == 0);
    struct Page *private = get_page(child->pgdir, writable, &child_pte);
    assert(private != NULL && private != shared);
    assert((*child_pte & (PTE_W | PTE_COW)) == PTE_W);
    assert(memcmp(page2kva(private), page2kva(shared), PGSIZE) == 0);
    assert(page_ref(shared) == 1 && page_ref(private) == 1);
    *(unsigned char *)page2kva(private) = 0x7f;
    assert(*(unsigned char *)page2kva(shared) == 0x5a);

    size_t before_write = nr_free_pages();
    assert(do_cow_fault(parent, writable) == 0);
    assert(get_page(parent->pgdir, writable, &parent_pte) == shared);
    assert((*parent_pte & (PTE_W | PTE_COW)) == PTE_W);
    assert(nr_free_pages() == before_write);

    exit_mmap(child);
    assert(copy_range(child->pgdir, parent->pgdir, writable, writable + PGSIZE, 0) == 0);
    private = get_page(child->pgdir, writable, &child_pte);
    assert(private != NULL && private != shared);
    assert((*child_pte & (PTE_W | PTE_COW)) == PTE_W);
    assert(memcmp(page2kva(private), page2kva(shared), PGSIZE) == 0);
    exit_mmap(child);
    exit_mmap(parent);
    free_page(child_root);
    free_page(parent_root);
    mm_destroy(child);
    mm_destroy(parent);
    assert(nr_free_pages() == initial_free_pages);
    lab5_pass("Copy on Write");
}

void test_lab5_process_control(void)
{
    current->need_resched = 0;
    assert(do_yield() == 0 && current->need_resched);
    current->need_resched = 0;

    assert(do_kill(-1) == -E_INVAL);
    assert(do_wait(-1, NULL) == -E_BAD_PROC);
    lab5_pass("Process Control");
    test_lab5_cow();
}
