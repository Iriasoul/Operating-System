/*
 * Real Trap-return tests: formal trapentry.S and formal trap.c, custom test
 * entry/linker script only in this fixture. No paging, no formal init.c.
 */

#include <defs.h>
#include <riscv.h>
#include <trap.h>

#include "harness.h"

extern int real_case_16bp(void);
extern int real_case_32bp(void);
extern int real_case_16ill(void);
extern int real_case_32ill(void);

static int
run_case(const char *name, int (*fn)(void)) {
    int failed = fn();
    if (failed) {
        test_printf("[real] %s: FAIL\n", name);
        return 1;
    }
    test_printf("[real] %s: ok\n", name);
    return 0;
}

void
test_main(void) {
    idt_init();
    test_printf("[real] idt_init installed __alltraps as stvec\n");

    int failures = 0;
    failures += run_case("16-bit breakpoint", real_case_16bp);
    failures += run_case("32-bit breakpoint at 2 mod 4", real_case_32bp);
    failures += run_case("16-bit illegal instruction", real_case_16ill);
    failures += run_case("32-bit illegal instruction at 2 mod 4",
                         real_case_32ill);

    if (failures != 0) {
        test_printf("REAL TRAP TESTS FAIL (%d)\n", failures);
        test_exit(1);
    }
    test_printf("REAL TRAP TESTS PASS\n");
    test_exit(0);
}
