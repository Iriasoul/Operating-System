#ifndef __TRAP_VALIDATION_HARNESS_H__
#define __TRAP_VALIDATION_HARNESS_H__

#include <defs.h>
#include <trap.h>

/* Test doubles: these live in the harness, not in the formal kernel. */
extern volatile size_t ticks;
extern unsigned long test_clock_calls;
extern unsigned long test_shutdown_calls;
extern unsigned long test_output_events;
extern unsigned long test_shutdown_order;

void capture_reset(void);
const char *capture_text(void);
size_t capture_length(void);
int capture_overrun(void);
int capture_starts_with(const char *prefix);
int capture_contains(const char *needle);

void raw_puts(const char *s);
void test_printf(const char *fmt, ...);
void test_exit(int fail) __attribute__((noreturn));

void frame_fill(struct trapframe *tf, uintptr_t epc, uintptr_t cause);
int frame_equal(const struct trapframe *a, const struct trapframe *b);

#endif
