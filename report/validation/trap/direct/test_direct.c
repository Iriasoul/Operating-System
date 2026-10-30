/*
 * Direct-call tests against the formal code/kern/trap/trap.c.
 *
 * The public handlers are called with constructed Trap frames. Console output,
 * clock re-arming and shutdown are test doubles from common/console.c.
 */

#include <defs.h>
#include <clock.h>
#include <riscv.h>
#include <stdio.h>
#include <string.h>
#include <trap.h>

#include "harness.h"

extern void interrupt_handler(struct trapframe *tf);
extern void exception_handler(struct trapframe *tf);

static uint16_t code_area[8] __attribute__((aligned(4)));
static int failures = 0;

static void
report(const char *name, int ok) {
    if (ok) {
        test_printf("[direct] %s: ok\n", name);
        return;
    }
    failures++;
    test_printf("[direct] %s: FAIL\n", name);
    if (capture_overrun()) {
        test_printf("[direct] capture buffer overflow\n");
    }
    raw_puts("[direct] captured output:\n");
    raw_puts(capture_text());
    raw_puts("\n");
}

static void
test_exception(const char *name, uintptr_t cause, uint16_t lo, uint16_t hi,
               int at_plus2, uintptr_t delta, const char *prefix,
               int expect_trapframe) {
    struct trapframe tf, expected;
    uintptr_t epc;

    memset(code_area, 0, sizeof(code_area));
    if (at_plus2) {
        code_area[1] = lo;
        code_area[2] = hi;
        epc = (uintptr_t)code_area + 2;
    } else {
        code_area[0] = lo;
        epc = (uintptr_t)code_area;
    }

    frame_fill(&tf, epc, cause);
    expected = tf;
    expected.epc += delta;

    capture_reset();
    exception_handler(&tf);

    int ok = frame_equal(&expected, &tf) && !capture_overrun() &&
             capture_starts_with(prefix);
    if (expect_trapframe && !capture_contains("trapframe at")) {
        ok = 0;
    }
    report(name, ok);
}

static void
test_other_exception(void) {
    struct trapframe tf, before;
    /* The sentinel must never be dereferenced by the default branch. */
    frame_fill(&tf, 0xdeadbeef, 20);
    before = tf;

    capture_reset();
    exception_handler(&tf);

    int ok = frame_equal(&before, &tf) && !capture_overrun() &&
             capture_contains("trapframe at") &&
             capture_contains("cause    0x00000014");
    report("other-exception-cause-20", ok);
}

struct irq_case {
    const char *name;
    uintptr_t cause;
    const char *text;
    int flagged;
};

static void
test_irq(const struct irq_case *c) {
    struct trapframe tf, before;
    uintptr_t cause = c->cause;
    if (c->flagged) {
        cause |= (uintptr_t)1 << (sizeof(uintptr_t) * 8 - 1);
    }

    frame_fill(&tf, 0x12345678, cause);
    before = tf;
    size_t ticks_before = ticks;
    unsigned long clock_before = test_clock_calls;
    unsigned long shutdown_before = test_shutdown_calls;

    capture_reset();
    interrupt_handler(&tf);

    int ok = frame_equal(&before, &tf) && !capture_overrun() &&
             capture_starts_with(c->text);
    if (ticks != ticks_before || test_clock_calls != clock_before ||
        test_shutdown_calls != shutdown_before) {
        ok = 0;
    }
    report(c->name, ok);
}

static void
test_unknown(const char *name, uintptr_t cause, const char *prefix) {
    struct trapframe tf, before;
    frame_fill(&tf, 0x12345678, cause);
    before = tf;
    size_t ticks_before = ticks;
    unsigned long clock_before = test_clock_calls;
    unsigned long shutdown_before = test_shutdown_calls;

    capture_reset();
    interrupt_handler(&tf);

    int ok = frame_equal(&before, &tf) && !capture_overrun() &&
             capture_starts_with(prefix) && capture_contains("trapframe at");
    if (ticks != ticks_before || test_clock_calls != clock_before ||
        test_shutdown_calls != shutdown_before) {
        ok = 0;
    }
    report(name, ok);
}

static const struct irq_case irq_cases[] = {
    {"irq-u-soft", IRQ_U_SOFT, "User software interrupt", 1},
    {"irq-s-soft", IRQ_S_SOFT, "Supervisor software interrupt", 1},
    {"irq-h-soft", IRQ_H_SOFT, "Hypervisor software interrupt", 0},
    {"irq-m-soft", IRQ_M_SOFT, "Machine software interrupt", 0},
    {"irq-u-timer", IRQ_U_TIMER, "User timer interrupt", 1},
    {"irq-h-timer", IRQ_H_TIMER, "Hypervisor timer interrupt", 0},
    {"irq-m-timer", IRQ_M_TIMER, "Machine timer interrupt", 0},
    {"irq-u-ext", IRQ_U_EXT, "User external interrupt", 1},
    {"irq-s-ext", IRQ_S_EXT, "Supervisor external interrupt", 1},
    {"irq-h-ext", IRQ_H_EXT, "Hypervisor external interrupt", 0},
    {"irq-m-ext", IRQ_M_EXT, "Machine external interrupt", 0},
};

void
test_main(void) {
    /* Run the size-truncation case first so a pre-fix log isolates it. */
    test_unknown("unknown-cause-0x10000000f",
                 0x10000000f,
                 "Unknown interrupt: cause 4294967311\n");
    test_unknown("unknown-cause-15", 15,
                 "Unknown interrupt: cause 15\n");
    test_unknown("unknown-cause-flagged-0x10000000f",
                 0x10000000f | ((uintptr_t)1 << (sizeof(uintptr_t) * 8 - 1)),
                 "Unknown interrupt: cause 4294967311\n");

    for (size_t i = 0; i < sizeof(irq_cases) / sizeof(irq_cases[0]); i++) {
        test_irq(&irq_cases[i]);
    }

    /* 16-bit breakpoint: c.ebreak. */
    test_exception("breakpoint-16bit", CAUSE_BREAKPOINT, 0x9002, 0, 0, 2,
                   "Breakpoint at 0x", 0);
    /* 32-bit breakpoint at an address that is 2 mod 4. */
    test_exception("breakpoint-32bit-2mod4", CAUSE_BREAKPOINT, 0x0073, 0x0010,
                   1, 4, "Breakpoint at 0x", 0);

    /* 16-bit illegal instruction: all-zero encoding. */
    test_exception("illegal-16bit", CAUSE_ILLEGAL_INSTRUCTION, 0x0000, 0, 0, 2,
                   "Illegal instruction at 0x", 0);
    /* 32-bit illegal instruction at an address that is 2 mod 4. */
    test_exception("illegal-32bit-2mod4", CAUSE_ILLEGAL_INSTRUCTION, 0x007b,
                   0x0000, 1, 4, "Illegal instruction at 0x", 0);

    /* Unsupported 48-bit-and-longer prefixes: diagnose, do not advance epc. */
    test_exception("illegal-long-encoding", CAUSE_ILLEGAL_INSTRUCTION, 0x001f,
                   0, 0, 0, "Illegal instruction at 0x", 1);
    test_exception("breakpoint-long-encoding", CAUSE_BREAKPOINT, 0x001f, 0, 0,
                   0, "Breakpoint at 0x", 1);

    test_other_exception();

    if (failures == 0) {
        test_printf("DIRECT TESTS PASS\n");
        test_exit(0);
    }
    test_printf("DIRECT TESTS FAIL (%d)\n", failures);
    test_exit(1);
}
