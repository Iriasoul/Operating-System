#include <defs.h>
#include <mmu.h>
#include <memlayout.h>
#include <clock.h>
#include <trap.h>
#include <riscv.h>
#include <stdio.h>
#include <assert.h>
#include <console.h>
#include <vmm.h>
#include <kdebug.h>
#include <unistd.h>
#include <syscall.h>
#include <error.h>
#include <sched.h>
#include <sync.h>
#include <sbi.h>

#define TICK_NUM 100

static void print_ticks()
{
    cprintf("%d ticks\n", TICK_NUM);
#ifdef DEBUG_GRADE
    cprintf("End of Test.\n");
    panic("EOT: kernel seems ok.");
#endif
}

/* idt_init - initialize IDT to each of the entry points in kern/trap/vectors.S */
void idt_init(void)
{
    extern void __alltraps(void);
    /* Set sscratch register to 0, indicating to exception vector that we are
     * presently executing in the kernel */
    write_csr(sscratch, 0);
    /* Set the exception vector address */
    write_csr(stvec, &__alltraps);
    /* Allow kernel to access user memory */
    set_csr(sstatus, SSTATUS_SUM);
}

/* trap_in_kernel - test if trap happened in kernel */
bool trap_in_kernel(struct trapframe *tf)
{
    return (tf->status & SSTATUS_SPP) != 0;
}

void print_trapframe(struct trapframe *tf)
{
    cprintf("trapframe at %p\n", tf);
    // cprintf("trapframe at 0x%x\n", tf);
    print_regs(&tf->gpr);
    cprintf("  status   0x%08x\n", tf->status);
    cprintf("  epc      0x%08x\n", tf->epc);
    cprintf("  tval 0x%08x\n", tf->tval);
    cprintf("  cause    0x%08x\n", tf->cause);
}

void print_regs(struct pushregs *gpr)
{
    cprintf("  zero     0x%08x\n", gpr->zero);
    cprintf("  ra       0x%08x\n", gpr->ra);
    cprintf("  sp       0x%08x\n", gpr->sp);
    cprintf("  gp       0x%08x\n", gpr->gp);
    cprintf("  tp       0x%08x\n", gpr->tp);
    cprintf("  t0       0x%08x\n", gpr->t0);
    cprintf("  t1       0x%08x\n", gpr->t1);
    cprintf("  t2       0x%08x\n", gpr->t2);
    cprintf("  s0       0x%08x\n", gpr->s0);
    cprintf("  s1       0x%08x\n", gpr->s1);
    cprintf("  a0       0x%08x\n", gpr->a0);
    cprintf("  a1       0x%08x\n", gpr->a1);
    cprintf("  a2       0x%08x\n", gpr->a2);
    cprintf("  a3       0x%08x\n", gpr->a3);
    cprintf("  a4       0x%08x\n", gpr->a4);
    cprintf("  a5       0x%08x\n", gpr->a5);
    cprintf("  a6       0x%08x\n", gpr->a6);
    cprintf("  a7       0x%08x\n", gpr->a7);
    cprintf("  s2       0x%08x\n", gpr->s2);
    cprintf("  s3       0x%08x\n", gpr->s3);
    cprintf("  s4       0x%08x\n", gpr->s4);
    cprintf("  s5       0x%08x\n", gpr->s5);
    cprintf("  s6       0x%08x\n", gpr->s6);
    cprintf("  s7       0x%08x\n", gpr->s7);
    cprintf("  s8       0x%08x\n", gpr->s8);
    cprintf("  s9       0x%08x\n", gpr->s9);
    cprintf("  s10      0x%08x\n", gpr->s10);
    cprintf("  s11      0x%08x\n", gpr->s11);
    cprintf("  t3       0x%08x\n", gpr->t3);
    cprintf("  t4       0x%08x\n", gpr->t4);
    cprintf("  t5       0x%08x\n", gpr->t5);
    cprintf("  t6       0x%08x\n", gpr->t6);
}

extern struct mm_struct *check_mm_struct;

//lab3 请补充你在lab3中的代码

static inline uintptr_t
interrupt_reason(uintptr_t cause) {
    return cause & ~((uintptr_t)1 << (sizeof(uintptr_t) * 8 - 1));
}

static size_t
instruction_length(uintptr_t epc) {
    uint16_t low = *(volatile uint16_t *)epc;

    if ((low & 0x3) != 0x3) {
        return 2;
    }

    if ((low & 0x1c) == 0x1c) {
        return 0;
    }

    return 4;
}

static void
advance_epc(struct trapframe *tf, size_t len) {
    if (len == 2 || len == 4) {
        tf->epc += len;
    } else {
        print_trapframe(tf);
    }
}

void interrupt_handler(struct trapframe *tf) {
    uintptr_t cause = interrupt_reason(tf->cause);

    switch (cause) {
    case IRQ_U_SOFT:
        cprintf("User software interrupt\n");
        break;

    case IRQ_S_SOFT:
        cprintf("Supervisor software interrupt\n");
        break;

    case IRQ_H_SOFT:
        cprintf("Hypervisor software interrupt\n");
        break;

    case IRQ_M_SOFT:
        cprintf("Machine software interrupt\n");
        break;

    case IRQ_U_TIMER:
        cprintf("User timer interrupt\n");
        break;

    case IRQ_S_TIMER:
        clock_set_next_event();
        ticks ++;
        if (current != NULL && !trap_in_kernel(tf)) {
            current->need_resched = 1;
        }

        if (ticks % TICK_NUM == 0) {
            print_ticks();
        }
        break;

    case IRQ_H_TIMER:
        cprintf("Hypervisor timer interrupt\n");
        break;

    case IRQ_M_TIMER:
        cprintf("Machine timer interrupt\n");
        break;

    case IRQ_U_EXT:
        cprintf("User external interrupt\n");
        break;

    case IRQ_S_EXT:
        cprintf("Supervisor external interrupt\n");
        break;

    case IRQ_H_EXT:
        cprintf("Hypervisor external interrupt\n");
        break;

    case IRQ_M_EXT:
        cprintf("Machine external interrupt\n");
        break;

    default:
        cprintf("Unknown interrupt: cause %lu\n",
                (unsigned long)cause);
        print_trapframe(tf);
        break;
    }
}

//lab5 function1 2410966 update exception_handler

void exception_handler(struct trapframe *tf) {
    switch (tf->cause) {
    case CAUSE_USER_ECALL:
        tf->epc += 4;
        syscall();
        return;

    case CAUSE_STORE_PAGE_FAULT:
        if (current != NULL && !trap_in_kernel(tf) &&
            do_cow_fault(current->mm, tf->tval) == 0) {
            return;
        }
        break;

    case CAUSE_ILLEGAL_INSTRUCTION:
        if (!trap_in_kernel(tf)) {
            break;
        }
        cprintf("Illegal instruction at 0x%016lx\n", tf->epc);
        advance_epc(tf, instruction_length(tf->epc));
        return;

    case CAUSE_BREAKPOINT:
        if (!trap_in_kernel(tf)) {
            break;
        }
        cprintf("Breakpoint at 0x%016lx\n", tf->epc);
        advance_epc(tf, instruction_length(tf->epc));
        return;

    default:
        break;
    }

    if (current != NULL && !trap_in_kernel(tf)) {
        cprintf("user exception %lu in pid %d at %p (tval %p)\n",
                (unsigned long)tf->cause, current->pid,
                (void *)tf->epc, (void *)tf->tval);
        do_exit(-E_KILLED);
        return;
    }

    print_trapframe(tf);
    if (tf->cause == CAUSE_FETCH_PAGE_FAULT || tf->cause == CAUSE_LOAD_PAGE_FAULT ||
        tf->cause == CAUSE_STORE_PAGE_FAULT) {
        panic("kernel page fault\n");
    }
}

static inline void trap_dispatch(struct trapframe *tf)
{
    if ((intptr_t)tf->cause < 0)
    {
        // interrupts
        interrupt_handler(tf);
    }
    else
    {
        // exceptions
        exception_handler(tf);
    }
}

/* *
 * trap - handles or dispatches an exception/interrupt. if and when trap() returns,
 * the code in kern/trap/trapentry.S restores the old CPU state saved in the
 * trapframe and then uses the sret instruction to return from the exception.
 * */
void trap(struct trapframe *tf)
{
    // dispatch based on what type of trap occurred
    //    cputs("some trap");
    if (current == NULL)
    {
        trap_dispatch(tf);
    }
    else
    {
        struct trapframe *otf = current->tf;
        current->tf = tf;

        bool in_kernel = trap_in_kernel(tf);

        trap_dispatch(tf);

        current->tf = otf;
        if (!in_kernel)
        {
            if (current->need_resched && !(current->flags & PF_EXITING))
            {
                schedule();
            }
            if (current->flags & PF_EXITING)
            {
                do_exit(-E_KILLED);
            }
        }
    }
}
