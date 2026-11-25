#include <proc.h>
#include <kmalloc.h>
#include <string.h>
#include <sync.h>
#include <pmm.h>
#include <error.h>
#include <sched.h>
#include <elf.h>
#include <vmm.h>
#include <trap.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>

/* ------------- process/thread mechanism design&implementation -------------
(an simplified Linux process/thread mechanism )
introduction:
  ucore implements a simple process/thread mechanism. process contains the independent memory sapce, at least one threads
for execution, the kernel data(for management), processor state (for context switch), files(in lab6), etc. ucore needs to
manage all these details efficiently. In ucore, a thread is just a special kind of process(share process's memory).
------------------------------
process state       :     meaning               -- reason
    PROC_UNINIT     :   uninitialized           -- alloc_proc
    PROC_SLEEPING   :   sleeping                -- try_free_pages, do_wait, do_sleep
    PROC_RUNNABLE   :   runnable(maybe running) -- proc_init, wakeup_proc,
    PROC_ZOMBIE     :   almost dead             -- do_exit

-----------------------------
process state changing:

  alloc_proc                                 RUNNING
      +                                   +--<----<--+
      +                                   + proc_run +
      V                                   +-->---->--+
PROC_UNINIT -- proc_init/wakeup_proc --> PROC_RUNNABLE -- try_free_pages/do_wait/do_sleep --> PROC_SLEEPING --
                                           A      +                                                           +
                                           |      +--- do_exit --> PROC_ZOMBIE                                +
                                           +                                                                  +
                                           -----------------------wakeup_proc----------------------------------
-----------------------------
process relations
parent:           proc->parent  (proc is children)
children:         proc->cptr    (proc is parent)
older sibling:    proc->optr    (proc is younger sibling)
younger sibling:  proc->yptr    (proc is older sibling)
-----------------------------
related syscall for process:
SYS_exit        : process exit,                           -->do_exit
SYS_fork        : create child process, dup mm            -->do_fork-->wakeup_proc
SYS_wait        : wait process                            -->do_wait
SYS_exec        : after fork, process execute a program   -->load a program and refresh the mm
SYS_clone       : create child thread                     -->do_fork-->wakeup_proc
SYS_yield       : process flag itself need resecheduling, -- proc->need_sched=1, then scheduler will rescheule this process
SYS_sleep       : process sleep                           -->do_sleep
SYS_kill        : kill process                            -->do_kill-->proc->flags |= PF_EXITING
                                                                 -->wakeup_proc-->do_wait-->do_exit
SYS_getpid      : get the process's pid

*/

// the process set's list
list_entry_t proc_list;

#define HASH_SHIFT 10
#define HASH_LIST_SIZE (1 << HASH_SHIFT)
#define pid_hashfn(x) (hash32(x, HASH_SHIFT))

// has list for process set based on pid
static list_entry_t hash_list[HASH_LIST_SIZE];

// idle proc
struct proc_struct *idleproc = NULL;
// init proc
struct proc_struct *initproc = NULL;
// current proc
struct proc_struct *current = NULL;

static int nr_process = 0;

void kernel_thread_entry(void);
void forkrets(struct trapframe *tf);
void switch_to(struct context *from, struct context *to);

//lab4 请补充你在lab4中的代码

//lab5 function2 2410966 update alloc_proc

//LAB4 function2 2412449
static struct proc_struct *
alloc_proc(void)
{
    struct proc_struct *proc = kmalloc(sizeof(struct proc_struct));
    if (proc != NULL)
    {
        memset(proc, 0, sizeof(struct proc_struct));
        proc->state = PROC_UNINIT;
        proc->pid = -1;
        proc->pgdir = boot_pgdir_pa;
        proc->wait_state = 0;
        proc->exit_code = 0;
        proc->cptr = proc->yptr = proc->optr = NULL;
        list_init(&(proc->list_link));
        list_init(&(proc->hash_link));
    }
    return proc;
}

// set_proc_name - set the name of proc
char *
set_proc_name(struct proc_struct *proc, const char *name)
{
    memset(proc->name, 0, sizeof(proc->name));
    return memcpy(proc->name, name, PROC_NAME_LEN);
}

// get_proc_name - get the name of proc
char *
get_proc_name(struct proc_struct *proc)
{
    static char name[PROC_NAME_LEN + 1];
    memset(name, 0, sizeof(name));
    return memcpy(name, proc->name, PROC_NAME_LEN);
}

// set_links - set the relation links of process
static void
set_links(struct proc_struct *proc)
{
    list_add(&proc_list, &(proc->list_link));
    proc->yptr = NULL;
    if ((proc->optr = proc->parent->cptr) != NULL)
    {
        proc->optr->yptr = proc;
    }
    proc->parent->cptr = proc;
    nr_process++;
}

// remove_links - clean the relation links of process
static void
remove_links(struct proc_struct *proc)
{
    list_del(&(proc->list_link));
    if (proc->optr != NULL)
    {
        proc->optr->yptr = proc->yptr;
    }
    if (proc->yptr != NULL)
    {
        proc->yptr->optr = proc->optr;
    }
    else
    {
        proc->parent->cptr = proc->optr;
    }
    nr_process--;
}

// get_pid - alloc a unique pid for process
static int
get_pid(void)
{
    static_assert(MAX_PID > MAX_PROCESS);
    struct proc_struct *proc;
    list_entry_t *list = &proc_list, *le;
    static int next_safe = MAX_PID, last_pid = MAX_PID;
    if (++last_pid >= MAX_PID)
    {
        last_pid = 1;
        goto inside;
    }
    if (last_pid >= next_safe)
    {
    inside:
        next_safe = MAX_PID;
    repeat:
        le = list;
        while ((le = list_next(le)) != list)
        {
            proc = le2proc(le, list_link);
            if (proc->pid == last_pid)
            {
                if (++last_pid >= next_safe)
                {
                    if (last_pid >= MAX_PID)
                    {
                        last_pid = 1;
                    }
                    next_safe = MAX_PID;
                    goto repeat;
                }
            }
            else if (proc->pid > last_pid && next_safe > proc->pid)
            {
                next_safe = proc->pid;
            }
        }
    }
    return last_pid;
}






// forkret -- the first kernel entry point of a new thread/process
// NOTE: the addr of forkret is setted in copy_thread function
//       after switch_to, the current proc will execute here.
static void
forkret(void)
{
    forkrets(current->tf);
}

// hash_proc - add proc into proc hash_list
static void
hash_proc(struct proc_struct *proc)
{
    list_add(hash_list + pid_hashfn(proc->pid), &(proc->hash_link));
}

// unhash_proc - delete proc from proc hash_list
static void
unhash_proc(struct proc_struct *proc)
{
    list_del(&(proc->hash_link));
}

// find_proc - find proc frome proc hash_list according to pid
struct proc_struct *
find_proc(int pid)
{
    if (0 < pid && pid < MAX_PID)
    {
        list_entry_t *list = hash_list + pid_hashfn(pid), *le = list;
        while ((le = list_next(le)) != list)
        {
            struct proc_struct *proc = le2proc(le, hash_link);
            if (proc->pid == pid)
            {
                return proc;
            }
        }
    }
    return NULL;
}

//lab5 function2 2410966 update kernel_thread
int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags)
{
    struct trapframe tf;
    memset(&tf, 0, sizeof(struct trapframe));
    tf.gpr.s0 = (uintptr_t)fn;
    tf.gpr.s1 = (uintptr_t)arg;
    tf.status = (read_csr(sstatus) | SSTATUS_SPP | SSTATUS_SPIE | SSTATUS_SUM) &
                ~SSTATUS_SIE;
    tf.epc = (uintptr_t)kernel_thread_entry;
    return do_fork(clone_flags | CLONE_VM, 0, &tf);
}

// setup_kstack - alloc pages with size KSTACKPAGE as process kernel stack
static int
setup_kstack(struct proc_struct *proc)
{
    struct Page *page = alloc_pages(KSTACKPAGE);
    if (page != NULL)
    {
        proc->kstack = (uintptr_t)page2kva(page);
        return 0;
    }
    return -E_NO_MEM;
}

// put_kstack - free the memory space of process kernel stack
static void
put_kstack(struct proc_struct *proc)
{
    free_pages(kva2page((void *)(proc->kstack)), KSTACKPAGE);
}

// setup_pgdir - alloc one page as PDT
static int
setup_pgdir(struct mm_struct *mm)
{
    struct Page *page;
    if ((page = alloc_page()) == NULL)
    {
        return -E_NO_MEM;
    }
    pde_t *pgdir = page2kva(page);
    memcpy(pgdir, boot_pgdir_va, PGSIZE);

    mm->pgdir = pgdir;
    return 0;
}

// put_pgdir - free the memory space of PDT
static void
put_pgdir(struct mm_struct *mm)
{
    free_page(kva2page(mm->pgdir));
}

//lab5 function3 YOUR CODE update copy_mm

static int
copy_mm(uint32_t clone_flags, struct proc_struct *proc)
{
    // Lab4 only creates kernel threads sharing the kernel page table.
    (void)clone_flags;
    assert(current->mm == NULL);
    proc->mm = NULL;
    return 0;
}

//lab5 function2 2410966

static int user_main(void *arg);

// init_main - the second kernel thread used to create user_main kernel threads
static int
init_main(void *arg)
{
    size_t free_pages = nr_free_pages();
    size_t allocated = kallocated();
    int pid = kernel_thread(user_main, NULL, 0);
    if (pid <= 0)
    {
        panic("cannot create user_main: %d.\n", pid);
    }
    char name[PROC_NAME_LEN + 1] = "user_main";
    set_proc_name(find_proc(pid), name);
    while (do_wait(0, NULL) == 0)
    {
        schedule();
    }
    cprintf("all user-mode processes have quit.\n");
    assert(current == initproc);
    assert(initproc->cptr == NULL && nr_process == 2);
    assert(nr_free_pages() == free_pages);
    assert(kallocated() == allocated);
    cprintf("init check memory pass.\n");
    return 0;
}

//lab5 function3 YOUR CODE

static void
copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf)
{
    proc->tf = (struct trapframe *)(proc->kstack + KSTACKSIZE -
                                   sizeof(struct trapframe));
    memcpy(proc->tf, tf, sizeof(struct trapframe));
    proc->tf->gpr.a0 = 0;
    proc->tf->gpr.sp = (esp != 0) ? esp : (uintptr_t)proc->tf;
    proc->context.ra = (uintptr_t)forkret;
    proc->context.sp = (uintptr_t)proc->tf;
}

int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf)
{
    int ret = -E_NO_FREE_PROC;
    struct proc_struct *proc;
    if (nr_process >= MAX_PROCESS)
    {
        return ret;
    }

    ret = -E_NO_MEM;
    if ((proc = alloc_proc()) == NULL)
    {
        goto fork_out;
    }
    proc->parent = current;
    if ((ret = setup_kstack(proc)) != 0)
    {
        goto bad_fork_cleanup_proc;
    }
    if ((ret = copy_mm(clone_flags, proc)) != 0)
    {
        goto bad_fork_cleanup_kstack;
    }
    copy_thread(proc, stack, tf);

    // Publish a fully initialized thread before making it runnable.
    bool intr_flag;
    local_intr_save(intr_flag);
    {
        proc->pid = get_pid();
        hash_proc(proc);
        list_add(&proc_list, &(proc->list_link));
        nr_process++;
        wakeup_proc(proc);
    }
    local_intr_restore(intr_flag);
    return proc->pid;

bad_fork_cleanup_kstack:
    put_kstack(proc);
bad_fork_cleanup_proc:
    kfree(proc);
fork_out:
    return ret;
}

//lab5 function4 YOUR CODE

// do_exit - called by sys_exit
//   1. call exit_mmap & put_pgdir & mm_destroy to free the almost all memory space of process
//   2. set process' state as PROC_ZOMBIE, then call wakeup_proc(parent) to ask parent reclaim itself.
//   3. call scheduler to switch to other process
int do_exit(int error_code)
{
    panic("process exit!!.\n");
}


#define __KERNEL_EXECVE(name, binary, size) ({           \
    cprintf("kernel_execve: pid = %d, name = \"%s\".\n", \
            current->pid, name);                         \
    kernel_execve(name, binary, (size_t)(size));         \
})

#define KERNEL_EXECVE(x) ({                                    \
    extern unsigned char _binary_obj___user_##x##_out_start[], \
        _binary_obj___user_##x##_out_size[];                   \
    __KERNEL_EXECVE(#x, _binary_obj___user_##x##_out_start,    \
                    _binary_obj___user_##x##_out_size);        \
})

#define __KERNEL_EXECVE2(x, xstart, xsize) ({   \
    extern unsigned char xstart[], xsize[];     \
    __KERNEL_EXECVE(#x, xstart, (size_t)xsize); \
})

#define KERNEL_EXECVE2(x, xstart, xsize) __KERNEL_EXECVE2(x, xstart, xsize)


//lab5 function2 2410966

static bool
exec_mem_check(struct mm_struct *mm, uintptr_t addr, size_t len, bool write)
{
    if (!user_mem_check(mm, addr, len, write))
    {
        return 0;
    }
    if (mm != NULL)
    {
        uintptr_t end = addr + len;
        for (uintptr_t va = ROUNDDOWN(addr, PGSIZE); va < end; va += PGSIZE)
        {
            pte_t *pte = get_pte(mm->pgdir, va, 0);
            uint32_t perm = PTE_V | PTE_U | (write ? PTE_W : PTE_R);
            if (pte == NULL || (*pte & perm) != perm)
            {
                return 0;
            }
        }
    }
    return 1;
}

int do_wait(int pid, int *code_store)
{
    if (pid < 0)
    {
        return -E_BAD_PROC;
    }
    if (code_store != NULL && !exec_mem_check(current->mm,
        (uintptr_t)code_store, sizeof(int), 1))
    {
        return -E_INVAL;
    }

    while (1)
    {
        bool intr_flag, has_child = 0;
        struct proc_struct *proc = NULL;
        local_intr_save(intr_flag);
        if (pid != 0)
        {
            proc = find_proc(pid);
            if (proc != NULL && proc->parent == current)
            {
                has_child = 1;
                if (proc->state != PROC_ZOMBIE)
                {
                    proc = NULL;
                }
            }
            else
            {
                proc = NULL;
            }
        }
        else
        {
            for (struct proc_struct *child = current->cptr;
                 child != NULL; child = child->optr)
            {
                has_child = 1;
                if (child->state == PROC_ZOMBIE)
                {
                    proc = child;
                    break;
                }
            }
        }
        if (proc != NULL)
        {
            assert(proc != idleproc && proc != initproc);
            if (code_store != NULL)
            {
                *code_store = proc->exit_code;
            }
            unhash_proc(proc);
            remove_links(proc);
            local_intr_restore(intr_flag);
            put_kstack(proc);
            kfree(proc);
            return 0;
        }
        if (!has_child)
        {
            local_intr_restore(intr_flag);
            return -E_BAD_PROC;
        }
        current->state = PROC_SLEEPING;
        current->wait_state = WT_CHILD;
        local_intr_restore(intr_flag);
        schedule();
        if (current->flags & PF_EXITING)
        {
            do_exit(-E_KILLED);
        }
    }
}

static int
load_icode(unsigned char *binary, size_t size)
{
    assert(current->mm == NULL);
    if (size < sizeof(struct elfhdr))
    {
        return -E_INVAL_ELF;
    }
    struct elfhdr *elf = (struct elfhdr *)binary;
    if (elf->e_magic != ELF_MAGIC ||
        elf->e_phentsize != sizeof(struct proghdr) ||
        elf->e_phoff > size ||
        elf->e_phnum > (size - elf->e_phoff) / sizeof(struct proghdr))
    {
        return -E_INVAL_ELF;
    }

    int ret = -E_NO_MEM;
    struct mm_struct *mm = mm_create();
    if (mm == NULL)
    {
        return ret;
    }
    if (setup_pgdir(mm) != 0)
    {
        goto bad_mm;
    }

    struct proghdr *ph = (struct proghdr *)(binary + elf->e_phoff);
    for (int i = 0; i < elf->e_phnum; i++, ph++)
    {
        if (ph->p_type != ELF_PT_LOAD)
        {
            continue;
        }
        ret = -E_INVAL_ELF;
        if (ph->p_filesz > ph->p_memsz || ph->p_offset > size ||
            ph->p_filesz > size - ph->p_offset)
        {
            goto bad_elf;
        }
        if (ph->p_memsz == 0)
        {
            continue;
        }
        if (ph->p_va < USERBASE || ph->p_va >= USTACKTOP - USTACKSIZE ||
            ph->p_memsz > USTACKTOP - USTACKSIZE - ph->p_va)
        {
            goto bad_elf;
        }

        uint32_t vm_flags = 0, perm = PTE_U | PTE_V;
        if (ph->p_flags & ELF_PF_R)
        {
            vm_flags |= VM_READ;
            perm |= PTE_R;
        }
        if (ph->p_flags & ELF_PF_W)
        {
            vm_flags |= VM_WRITE;
            perm |= PTE_R | PTE_W;
        }
        if (ph->p_flags & ELF_PF_X)
        {
            vm_flags |= VM_EXEC;
            perm |= PTE_X;
        }
        if ((perm & (PTE_R | PTE_W | PTE_X)) == 0)
        {
            goto bad_elf;
        }
        if ((ret = mm_map(mm, ph->p_va, ph->p_memsz, vm_flags, NULL)) != 0)
        {
            goto bad_elf;
        }

        uintptr_t start = ph->p_va, file_end = start + ph->p_filesz;
        uintptr_t end = start + ph->p_memsz;
        for (uintptr_t va = ROUNDDOWN(start, PGSIZE); va < end; va += PGSIZE)
        {
            struct Page *page = pgdir_alloc_page(mm->pgdir, va, perm);
            if (page == NULL)
            {
                ret = -E_NO_MEM;
                goto bad_elf;
            }
            unsigned char *dst = page2kva(page);
            memset(dst, 0, PGSIZE);
            uintptr_t from = va < start ? start : va;
            uintptr_t to = va + PGSIZE < file_end ? va + PGSIZE : file_end;
            if (from < to)
            {
                memcpy(dst + from - va, binary + ph->p_offset + from - start,
                       to - from);
            }
        }
    }

    struct vma_struct *entry = find_vma(mm, elf->e_entry);
    if (entry == NULL || !(entry->vm_flags & VM_EXEC))
    {
        ret = -E_INVAL_ELF;
        goto bad_elf;
    }
    ret = mm_map(mm, USTACKTOP - USTACKSIZE, USTACKSIZE,
                 VM_READ | VM_WRITE | VM_STACK, NULL);
    if (ret != 0)
    {
        goto bad_elf;
    }
    for (uintptr_t va = USTACKTOP - 4 * PGSIZE; va < USTACKTOP; va += PGSIZE)
    {
        struct Page *page = pgdir_alloc_page(mm->pgdir, va, PTE_U | PTE_R | PTE_W);
        if (page == NULL)
        {
            ret = -E_NO_MEM;
            goto bad_elf;
        }
        memset(page2kva(page), 0, PGSIZE);
    }

    mm_count_inc(mm);
    current->mm = mm;
    current->pgdir = PADDR(mm->pgdir);
    lsatp(current->pgdir);
    flush_tlb();
    asm volatile("fence.i" ::: "memory");

    struct trapframe *tf = current->tf;
    uintptr_t status = read_csr(sstatus);
    memset(tf, 0, sizeof(*tf));
    tf->gpr.sp = USTACKTOP;
    tf->epc = elf->e_entry;
    tf->status = (status | SSTATUS_SPIE | SSTATUS_SUM) &
                 ~(SSTATUS_SPP | SSTATUS_SIE);
    return 0;

bad_elf:
    exit_mmap(mm);
    put_pgdir(mm);
bad_mm:
    mm_destroy(mm);
    return ret;
}

int do_execve(const char *name, size_t len, unsigned char *binary, size_t size)
{
    struct mm_struct *mm = current->mm;
    char local_name[PROC_NAME_LEN + 1];
    memset(local_name, 0, sizeof(local_name));
    if (len > PROC_NAME_LEN)
    {
        len = PROC_NAME_LEN;
    }
    if (len == 0 || !exec_mem_check(mm, (uintptr_t)name, len, 0) ||
        size < sizeof(struct elfhdr) ||
        !exec_mem_check(mm, (uintptr_t)binary, size, 0))
    {
        return -E_INVAL;
    }
    memcpy(local_name, name, len);

    unsigned char *image = binary;
    if (mm != NULL)
    {
        image = kmalloc(ROUNDUP(size, PGSIZE));
        if (image == NULL)
        {
            return -E_NO_MEM;
        }
        memcpy(image, binary, size);
        lsatp(boot_pgdir_pa);
        flush_tlb();
        current->mm = NULL;
        current->pgdir = boot_pgdir_pa;
        if (mm_count_dec(mm) == 0)
        {
            exit_mmap(mm);
            put_pgdir(mm);
            mm_destroy(mm);
        }
    }

    int ret = load_icode(image, size);
    if (mm != NULL)
    {
        kfree(image);
    }
    if (ret != 0)
    {
        do_exit(ret);
        return ret;
    }
    set_proc_name(current, local_name);
    return 0;
}

static int
kernel_execve(const char *name, unsigned char *binary, size_t size)
{
    bool intr_flag;
    local_intr_save(intr_flag);
    current->tf = (struct trapframe *)(current->kstack + KSTACKSIZE -
                                       sizeof(struct trapframe));
    int ret = do_execve(name, strlen(name), binary, size);
    if (ret == 0)
    {
        forkrets(current->tf);
    }
    local_intr_restore(intr_flag);
    return ret;
}

static int
user_main(void *arg)
{
#ifdef TEST
    return KERNEL_EXECVE2(TEST, TESTSTART, TESTSIZE);
#else
    return KERNEL_EXECVE(exit);
#endif
}


//lab4 进程初始化与调度（从 lab4 分支搬回）
void proc_init(void)
{
    list_init(&proc_list);
    for (int i = 0; i < HASH_LIST_SIZE; i++)
    {
        list_init(hash_list + i);
    }

    if ((idleproc = alloc_proc()) == NULL)
    {
        panic("cannot allocate idleproc.\n");
    }
    idleproc->pid = 0;
    idleproc->state = PROC_RUNNABLE;
    idleproc->kstack = (uintptr_t)bootstack;
    idleproc->need_resched = 1;
    set_proc_name(idleproc, "idle");
    nr_process++;
    current = idleproc;

    int pid = kernel_thread(init_main, "Hello world!!", 0);
    if (pid <= 0)
    {
        panic("cannot create initproc: %d.\n", pid);
    }
    initproc = find_proc(pid);
    assert(initproc != NULL);
    set_proc_name(initproc, "init");
}

//LAB4 function3 2410668
void cpu_idle(void)
{
    while (1)
    {
        if (current->need_resched)
        {
            schedule();
        }
    }
}

void proc_run(struct proc_struct *proc)
{
    if (proc != current)
    {
        bool intr_flag;
        local_intr_save(intr_flag);
        {
            struct proc_struct *prev = current;
            current = proc;
            lsatp(proc->pgdir);
            switch_to(&(prev->context), &(proc->context));
        }
        local_intr_restore(intr_flag);
    }
}

