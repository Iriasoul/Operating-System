/*
 * Clock-boundary test against the formal trap.c.
 *
 * Fresh test instance: ticks and trap.c's static report counter start at zero
 * because start.S clears .bss. clock_set_next_event() and sbi_shutdown() are
 * recording test doubles; this does not prove real timer or SBI behaviour.
 */

#include <defs.h>
#include <clock.h>
#include <riscv.h>
#include <trap.h>

#include "harness.h"

extern void interrupt_handler(struct trapframe *tf);

void
test_main(void) {
    struct trapframe tf, before;
    uintptr_t cause = IRQ_S_TIMER |
        ((uintptr_t)1 << (sizeof(uintptr_t) * 8 - 1));
    frame_fill(&tf, 0x12345678, cause);
    before = tf;

    unsigned long stats = 0;
    for (unsigned long i = 1; i <= 1000; i++) {
        test_clock_calls = 0;
        test_shutdown_calls = 0;
        test_shutdown_order = 0;
        test_output_events = 0;
        capture_reset();

        interrupt_handler(&tf);

        int expect_stat = (i % 100 == 0);
        int has_stat = capture_contains("100 ticks");
        int ok = (test_clock_calls == 1) && (ticks == i) &&
                 frame_equal(&before, &tf) && !capture_overrun();

        if (expect_stat) {
            stats++;
            if (!has_stat || test_output_events != 1 || capture_length() == 0) {
                ok = 0;
            }
        } else if (has_stat || test_output_events != 0 ||
                   capture_length() != 0) {
            ok = 0;
        }
        if (i < 1000 && test_shutdown_calls != 0) {
            ok = 0;
        }
        if (i == 1000) {
            if (test_shutdown_calls != 1) {
                ok = 0;
            }
            /* The shutdown stub records how many outputs preceded it. */
            if (test_shutdown_order != test_output_events) {
                ok = 0;
            }
            if (stats != 10) {
                ok = 0;
            }
        }

        if (!ok) {
            test_printf("[clock] FAIL at tick=%lu clock_calls=%lu ticks=%lu "
                        "stats=%lu shutdown=%lu order=%lu/%lu\n",
                        i, test_clock_calls, (unsigned long)ticks, stats,
                        test_shutdown_calls, test_shutdown_order,
                        test_output_events);
            raw_puts("[clock] captured output:\n");
            raw_puts(capture_text());
            raw_puts("\n");
            test_exit(1);
        }

        if (expect_stat) {
            test_printf("[clock] tick=%lu stats=%lu shutdown=%lu order=%lu/%lu\n",
                        i, stats, test_shutdown_calls, test_shutdown_order,
                        test_output_events);
        }
    }

    test_printf("[clock] checked 1000 interrupts: one re-arm per tick, "
                "ticks 1..1000, ten reports, shutdown only at tick 1000 "
                "after the report\n");
    test_printf("CLOCK BOUNDARY PASS\n");
    test_exit(0);
}
