#include <unistd.h>
#include <proc.h>
#include <syscall.h>
#include <trap.h>
#include <stdio.h>
#include <pmm.h>
#include <assert.h>
#include <error.h>
#include <vmm.h>


static int sys_exit(uint64_t arg[]);
static int sys_fork(uint64_t arg[]);
static int sys_wait(uint64_t arg[]);
static int sys_exec(uint64_t arg[]);
static int sys_yield(uint64_t arg[]);
static int sys_kill(uint64_t arg[]);
static int sys_getpid(uint64_t arg[]);
static int sys_putc(uint64_t arg[]);
static int sys_pgdir(uint64_t arg[]);

static int (*syscalls[])(uint64_t arg[]) = {
    [SYS_exit]              sys_exit,
    [SYS_fork]              sys_fork,
    [SYS_wait]              sys_wait,
    [SYS_exec]              sys_exec,
    [SYS_yield]             sys_yield,
    [SYS_kill]              sys_kill,
    [SYS_getpid]            sys_getpid,
    [SYS_putc]              sys_putc,
    [SYS_pgdir]             sys_pgdir,
};

#define NUM_SYSCALLS        ((sizeof(syscalls)) / (sizeof(syscalls[0])))

//lab5 function1 2410966
static int sys_exit(uint64_t arg[])
{
    return do_exit((int)arg[0]);
}

static int sys_fork(uint64_t arg[])
{
    return do_fork(0, current->tf->gpr.sp, current->tf);
}

static int sys_wait(uint64_t arg[])
{
    return do_wait((int)arg[0], (int *)arg[1]);
}

static int sys_exec(uint64_t arg[])
{
    return do_execve((const char *)arg[0], (size_t)arg[1],
                     (unsigned char *)arg[2], (size_t)arg[3]);
}

static int sys_yield(uint64_t arg[])
{
    return do_yield();
}

static int sys_kill(uint64_t arg[])
{
    return do_kill((int)arg[0]);
}

static int sys_getpid(uint64_t arg[])
{
    return current->pid;
}

static int sys_putc(uint64_t arg[])
{
    cputchar((int)arg[0]);
    return 0;
}

static int sys_pgdir(uint64_t arg[])
{
    struct mm_struct *mm = current->mm;
    if (mm == NULL)
    {
        return -E_INVAL;
    }
    cprintf("pgdir: %p\n", mm->pgdir);
    list_entry_t *head = &mm->mmap_list, *le = head;
    while ((le = list_next(le)) != head)
    {
        struct vma_struct *vma = le2vma(le, list_link);
        for (uintptr_t va = vma->vm_start; va < vma->vm_end; va += PGSIZE)
        {
            pte_t *pte = get_pte(mm->pgdir, va, 0);
            if (pte != NULL && (*pte & PTE_V))
            {
                cprintf("%p -> %p %c%c%c%c\n", (void *)va,
                        (void *)PTE_ADDR(*pte),
                        (*pte & PTE_U) ? 'u' : '-',
                        (*pte & PTE_R) ? 'r' : '-',
                        (*pte & PTE_W) ? 'w' : '-',
                        (*pte & PTE_X) ? 'x' : '-');
            }
        }
    }
    return 0;
}

void syscall(void)
{
    struct trapframe *tf = current->tf;
    uint64_t num = tf->gpr.a0;
    uint64_t arg[5] = {tf->gpr.a1, tf->gpr.a2, tf->gpr.a3,
                       tf->gpr.a4, tf->gpr.a5};
    if (num < NUM_SYSCALLS && syscalls[num] != NULL)
    {
        tf->gpr.a0 = syscalls[num](arg);
    }
    else
    {
        tf->gpr.a0 = -E_INVAL;
    }
}
