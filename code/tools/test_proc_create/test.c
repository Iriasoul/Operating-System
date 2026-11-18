// Execute the production module with simulated heap/pages/interrupt hardware.
#include "../../kern/process/proc.c"
#include "../../kern/schedule/sched.c"

extern void *malloc(size_t n);
extern void free(void *ptr);
extern int vprintf(const char *fmt, va_list ap);
extern void exit(int status) __attribute__((noreturn));

uintptr_t test_sstatus;
uintptr_t boot_pgdir_pa = 0x80208000;
char bootstack[KSTACKSIZE] __attribute__((aligned(16)));

#define TEST_STACKS 16
static struct Page test_pages[TEST_STACKS];
static char test_stacks[TEST_STACKS][KSTACKSIZE] __attribute__((aligned(16)));
static int stack_used[TEST_STACKS];
static int live_pcbs, live_stacks, heap_calls, heap_frees;
static int fail_heap, fail_stack, disable_calls, enable_calls;
static int context_switches, satp_loads;
static uintptr_t test_satp;
static struct context *last_from, *last_to;

int cprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int ret = vprintf(fmt, ap);
    va_end(ap);
    return ret;
}

void __panic(const char *file, int line, const char *fmt, ...)
{
    cprintf("FAIL %s:%d: ", file, line);
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    cprintf("\n");
    exit(1);
}

void *kmalloc(size_t n)
{
    heap_calls++;
    assert(n == sizeof(struct proc_struct));
    if (fail_heap)
    {
        fail_heap = 0;
        return NULL;
    }
    void *ptr = malloc(n);
    assert(ptr != NULL);
    memset(ptr, 0x5a, n);
    live_pcbs++;
    return ptr;
}

void kfree(void *ptr)
{
    assert(ptr != NULL && live_pcbs > 0);
    live_pcbs--;
    heap_frees++;
    free(ptr);
}

struct Page *alloc_pages(size_t n)
{
    assert(n == KSTACKPAGE);
    if (fail_stack)
    {
        fail_stack = 0;
        return NULL;
    }
    for (int i = 0; i < TEST_STACKS; i++)
    {
        if (!stack_used[i])
        {
            stack_used[i] = 1;
            live_stacks++;
            memset(test_stacks[i], 0x5a, KSTACKSIZE);
            return test_pages + i;
        }
    }
    return NULL;
}

void free_pages(struct Page *page, size_t n)
{
    int i = page - test_pages;
    assert(n == KSTACKPAGE && i >= 0 && i < TEST_STACKS);
    assert(stack_used[i]);
    stack_used[i] = 0;
    live_stacks--;
}

void *test_page2kva(struct Page *page)
{
    int i = page - test_pages;
    assert(i >= 0 && i < TEST_STACKS && stack_used[i]);
    return test_stacks[i];
}

struct Page *test_kva2page(void *kva)
{
    for (int i = 0; i < TEST_STACKS; i++)
    {
        if (kva == test_stacks[i])
        {
            assert(stack_used[i]);
            return test_pages + i;
        }
    }
    panic("unknown kernel stack");
}

void intr_disable(void)
{
    disable_calls++;
    test_sstatus &= ~SSTATUS_SIE;
}

void intr_enable(void)
{
    enable_calls++;
    test_sstatus |= SSTATUS_SIE;
}

// Entry points are addresses only: this harness never switches CPU context.
void kernel_thread_entry(void) {}
void forkrets(struct trapframe *tf) { panic("unexpected context switch"); }

void test_lsatp(uintptr_t pgdir)
{
    assert(!(test_sstatus & SSTATUS_SIE));
    assert(current->pgdir == pgdir);
    test_satp = ((uintptr_t)SATP_MODE_SV39 << 60) | (pgdir >> PGSHIFT);
    satp_loads++;
}

void switch_to(struct context *from, struct context *to)
{
    assert(!(test_sstatus & SSTATUS_SIE));
    assert(from != to && to == &current->context);
    assert(test_satp == (((uintptr_t)SATP_MODE_SV39 << 60) |
                         (current->pgdir >> PGSHIFT)));
    last_from = from;
    last_to = to;
    context_switches++;
}

static int count_procs(void)
{
    int count = 0;
    list_entry_t *le = &proc_list;
    while ((le = list_next(le)) != &proc_list)
    {
        assert(++count < MAX_PROCESS);
        assert(list_prev(list_next(le)) == le);
        struct proc_struct *proc = le2proc(le, list_link);
        assert(find_proc(proc->pid) == proc);
        assert(proc->state == PROC_RUNNABLE);
    }
    return count;
}

static int count_hashed(void)
{
    int count = 0;
    for (int i = 0; i < HASH_LIST_SIZE; i++)
    {
        list_entry_t *le = hash_list + i;
        while ((le = list_next(le)) != hash_list + i)
        {
            assert(++count < MAX_PROCESS);
            struct proc_struct *proc = le2proc(le, hash_link);
            assert(pid_hashfn(proc->pid) == (uint32_t)i);
        }
    }
    return count;
}

static int sample_thread(void *arg) { return 0; }

static void test_alloc_initialization(void)
{
    fail_heap = 1;
    assert(alloc_proc() == NULL && live_pcbs == 0);
    struct proc_struct *proc = alloc_proc();
    assert(proc != NULL && proc->state == PROC_UNINIT && proc->pid == -1);
    assert(proc->pgdir == boot_pgdir_pa && proc->runs == 0);
    assert(proc->kstack == 0 && proc->need_resched == 0);
    assert(proc->parent == NULL && proc->mm == NULL && proc->tf == NULL);
    assert(proc->flags == 0);
    struct context empty = {0};
    assert(memcmp(&proc->context, &empty, sizeof(empty)) == 0);
    for (int i = 0; i <= PROC_NAME_LEN; i++) assert(proc->name[i] == 0);
    assert(list_next(&proc->list_link) == &proc->list_link);
    assert(list_prev(&proc->list_link) == &proc->list_link);
    assert(list_next(&proc->hash_link) == &proc->hash_link);
    assert(list_prev(&proc->hash_link) == &proc->hash_link);
    assert(set_proc_name(proc, "idle") == proc->name);
    assert(strcmp(proc->name, "idle") == 0);
    set_proc_name(proc, "0123456789abcdef");
    assert(strncmp(proc->name, "0123456789abcde", PROC_NAME_LEN) == 0);
    assert(proc->name[PROC_NAME_LEN] == 0);
    set_proc_name(proc, "");
    for (int i = 0; i <= PROC_NAME_LEN; i++) assert(proc->name[i] == 0);
    kfree(proc);
    assert(live_pcbs == 0);
    cprintf("[Module2] PCB initialization and allocation failure PASS\n");
}

static void test_frame_copy(void)
{
    struct proc_struct *proc = alloc_proc();
    assert(proc != NULL && setup_kstack(proc) == 0);
    struct trapframe tf = {0};
    tf.gpr.a0 = 123;
    tf.gpr.s0 = 456;
    tf.gpr.sp = 789;
    tf.status = 0xabc;
    tf.epc = 0x1234;
    struct trapframe original = tf;
    copy_thread(proc, 0x100000, &tf);
    struct trapframe expected = tf;
    expected.gpr.a0 = 0;
    expected.gpr.sp = 0x100000;
    assert(memcmp(proc->tf, &expected, sizeof(expected)) == 0);
    assert(memcmp(&tf, &original, sizeof(tf)) == 0);
    assert((uintptr_t)proc->tf == proc->kstack + KSTACKSIZE - sizeof(tf));
    assert(proc->context.ra == (uintptr_t)forkret);
    assert(proc->context.sp == (uintptr_t)proc->tf && proc->context.sp % 16 == 0);
    copy_thread(proc, 0, &tf);
    assert(proc->tf->gpr.sp == (uintptr_t)proc->tf);
    assert(memcmp(&tf, &original, sizeof(tf)) == 0);
    put_kstack(proc);
    kfree(proc);
    cprintf("[Module2] Independent trapframe and both stack modes PASS\n");
}

static void test_registration_and_failures(void)
{
    uintptr_t status = SSTATUS_SUM | SSTATUS_SIE | 0x40;
    test_sstatus = status;
    int disabled = disable_calls, enabled = enable_calls;
    int arg = 42;
    int pid = kernel_thread(sample_thread, &arg, 0);
    struct proc_struct *proc = find_proc(pid);
    assert(pid == 2 && proc != NULL);
    assert(proc->parent == idleproc && proc->mm == NULL);
    assert(proc->pgdir == boot_pgdir_pa && proc->state == PROC_RUNNABLE);
    assert(proc->tf->gpr.s0 == (uintptr_t)sample_thread);
    assert(proc->tf->gpr.s1 == (uintptr_t)&arg && proc->tf->gpr.a0 == 0);
    assert(proc->tf->epc == (uintptr_t)kernel_thread_entry);
    assert(proc->tf->status == ((status | SSTATUS_SPP | SSTATUS_SPIE) & ~SSTATUS_SIE));
    assert(test_sstatus == status && current == idleproc);
    assert(disable_calls == disabled + 1 && enable_calls == enabled + 1);
    assert(proc->kstack != initproc->kstack && proc->tf != initproc->tf);
    assert(count_procs() == 2 && count_hashed() == 2 && nr_process == 3);
    cprintf("[Module2] PID/hash/list, thread entry and enabled IRQ restoration PASS\n");

    int pcbs = live_pcbs, stacks = live_stacks, frees = heap_frees;
    fail_heap = 1;
    assert(kernel_thread(sample_thread, NULL, 0) == -E_NO_MEM);
    assert(live_pcbs == pcbs && live_stacks == stacks && heap_frees == frees);
    assert(nr_process == 3 && count_procs() == 2 && count_hashed() == 2);
    assert(test_sstatus == status);

    fail_stack = 1;
    assert(kernel_thread(sample_thread, NULL, 0) == -E_NO_MEM);
    assert(live_pcbs == pcbs && live_stacks == stacks && heap_frees == frees + 1);
    assert(nr_process == 3 && count_procs() == 2 && count_hashed() == 2);
    assert(find_proc(pid + 1) == NULL && test_sstatus == status);
    cprintf("[Module2] PCB/stack failures leave no resources or registration PASS\n");

    int process_count = nr_process, calls = heap_calls;
    nr_process = MAX_PROCESS;
    assert(kernel_thread(sample_thread, NULL, 0) == -E_NO_FREE_PROC);
    assert(heap_calls == calls && live_pcbs == pcbs && live_stacks == stacks);
    nr_process = process_count;
    assert(count_procs() == 2 && count_hashed() == 2 && test_sstatus == status);
    cprintf("[Module2] Process limit rejects before allocating PASS\n");

    test_sstatus = SSTATUS_SUM;
    disabled = disable_calls;
    enabled = enable_calls;
    int next_pid = kernel_thread(sample_thread, NULL, CLONE_THREAD);
    assert(next_pid == pid + 1 && find_proc(next_pid) != NULL);
    assert(find_proc(next_pid)->tf->gpr.s1 == 0);
    assert(test_sstatus == SSTATUS_SUM);
    assert(disable_calls == disabled && enable_calls == enabled);
    assert(nr_process == 4 && count_procs() == 3 && count_hashed() == 3);
    assert(find_proc(0) == NULL && find_proc(MAX_PID) == NULL);
    cprintf("[Module2] PID remains unconsumed on failure and disabled IRQ preserved PASS\n");
}

static void test_schedule_selection(void)
{
    struct proc_struct *first = le2proc(list_next(&proc_list), list_link);
    struct proc_struct *middle = le2proc(list_next(&first->list_link), list_link);
    struct proc_struct *last = le2proc(list_next(&middle->list_link), list_link);
    assert(list_next(&last->list_link) == &proc_list);
    assert(first != middle && middle != last);

    current = idleproc;
    idleproc->need_resched = 1;
    test_sstatus = SSTATUS_SUM | SSTATUS_SIE;
    int switches = context_switches, runs = first->runs;
    schedule();
    assert(current == first && first->runs == runs + 1);
    assert(idleproc->need_resched == 0 && context_switches == switches + 1);
    assert(last_from == &idleproc->context && last_to == &first->context);
    assert(test_sstatus == (SSTATUS_SUM | SSTATUS_SIE));
    cprintf("[Module3] Idle selects a runnable thread and switches atomically PASS\n");

    middle->state = PROC_SLEEPING;
    last->state = PROC_ZOMBIE;
    switches = context_switches;
    int loads = satp_loads;
    runs = first->runs;
    first->need_resched = 1;
    schedule();
    assert(current == first && first->need_resched == 0);
    assert(first->runs == runs + 1 && context_switches == switches);
    assert(satp_loads == loads);
    proc_run(first);
    assert(context_switches == switches && satp_loads == loads);
    cprintf("[Module3] Skips blocked/zombie threads and avoids self-switch PASS\n");

    first->state = PROC_SLEEPING;
    schedule();
    assert(current == idleproc && context_switches == switches + 1);
    switches = context_switches;
    schedule();
    assert(current == idleproc && context_switches == switches);
    cprintf("[Module3] All blocked threads fall back to idle PASS\n");

    middle->state = last->state = PROC_RUNNABLE;
    proc_run(last);
    test_sstatus = SSTATUS_SUM;
    int enables = enable_calls;
    runs = middle->runs;
    schedule();
    assert(current == middle && middle->runs == runs + 1);
    assert(test_sstatus == SSTATUS_SUM && enable_calls == enables);
    assert(last_from == &last->context && last_to == &middle->context);
    cprintf("[Module3] Wraps around list head and preserves disabled IRQ PASS\n");

    proc_run(idleproc);
    list_entry_t saved_list = proc_list;
    list_init(&proc_list);
    switches = context_switches;
    runs = idleproc->runs;
    schedule();
    assert(current == idleproc && idleproc->runs == runs + 1);
    assert(context_switches == switches);
    proc_list = saved_list;
    first->state = middle->state = last->state = PROC_RUNNABLE;
    assert(count_procs() == 3 && satp_loads == context_switches);
    cprintf("[Module3] Empty process list keeps idle running PASS\n");
}

// Reuse the supplied process-creation assertions. Unused PMM/schedule tests
// are discarded by --gc-sections; they are not executed by this harness.
#include "../test_lab4.c"

int main(void)
{
    test_alloc_initialization();
    test_sstatus = SSTATUS_SUM | SSTATUS_SIE;
    proc_init();
    test_lab4_process_create();
    assert(nr_process == 2 && count_procs() == 1 && count_hashed() == 1);
    assert(live_pcbs == 2 && live_stacks == 1);
    assert(strcmp(idleproc->name, "idle") == 0 && strcmp(initproc->name, "init") == 0);
    assert(initproc->tf->gpr.s0 == (uintptr_t)init_main);
    assert(strcmp((char *)initproc->tf->gpr.s1, "Hello world!!") == 0);
    test_frame_copy();
    test_registration_and_failures();
    cprintf("[Module2] All isolated process-creation tests PASS\n");
    test_schedule_selection();

    while (!list_empty(&proc_list))
    {
        struct proc_struct *proc = le2proc(list_next(&proc_list), list_link);
        list_del(&proc->list_link);
        list_del(&proc->hash_link);
        put_kstack(proc);
        kfree(proc);
    }
    kfree(idleproc);
    current = idleproc = initproc = NULL;
    nr_process = 0;
    assert(live_pcbs == 0 && live_stacks == 0 && count_hashed() == 0);
    cprintf("[Module2/3] All isolated tests and resource cleanup PASS\n");
    return 0;
}
