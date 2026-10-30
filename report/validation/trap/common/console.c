/*
 * Test-double console, clock and shutdown interfaces.
 *
 * cprintf() uses the real code/libs/printfmt.c formatter, but writes into a
 * bounded capture buffer and to the SBI console. clock_set_next_event() and
 * sbi_shutdown() only record calls; they do not touch hardware timers or
 * request a real shutdown. These are test doubles, not kernel behaviour.
 */

#include <defs.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <clock.h>
#include <sbi.h>

#include "harness.h"

volatile size_t ticks = 0;
unsigned long test_clock_calls = 0;
unsigned long test_shutdown_calls = 0;
unsigned long test_output_events = 0;
unsigned long test_shutdown_order = 0;

static char capture_buf[8192];
static size_t capture_len = 0;
static int capture_overflow = 0;

void
capture_reset(void) {
    capture_len = 0;
    capture_overflow = 0;
    capture_buf[0] = '\0';
}

const char *
capture_text(void) {
    return capture_buf;
}

size_t
capture_length(void) {
    return capture_len;
}

int
capture_overrun(void) {
    return capture_overflow;
}

static void
raw_putc(char c) {
    register unsigned long a0 asm("a0") = (unsigned char)c;
    register unsigned long a7 asm("a7") = 1;  /* SBI console putchar */
    asm volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
}

void
raw_puts(const char *s) {
    while (*s != '\0') {
        raw_putc(*s++);
    }
}

int
cprintf(const char *fmt, ...) {
    char tmp[4096];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);

    if (n < 0) {
        return n;
    }
    if ((size_t)n >= sizeof(tmp)) {
        capture_overflow = 1;
    }
    size_t copy = (size_t)n < sizeof(tmp) ? (size_t)n : sizeof(tmp) - 1;
    if (capture_len + copy + 1 > sizeof(capture_buf)) {
        capture_overflow = 1;
    } else {
        memcpy(capture_buf + capture_len, tmp, copy);
        capture_len += copy;
        capture_buf[capture_len] = '\0';
    }
    test_output_events++;
    for (size_t i = 0; i < copy; i++) {
        raw_putc(tmp[i]);
    }
    return n;
}

void
test_printf(const char *fmt, ...) {
    char tmp[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n <= 0) {
        return;
    }
    size_t len = (size_t)n < sizeof(tmp) ? (size_t)n : sizeof(tmp) - 1;
    for (size_t i = 0; i < len; i++) {
        raw_putc(tmp[i]);
    }
}

void
clock_set_next_event(void) {
    test_clock_calls++;
}

void
sbi_shutdown(void) {
    test_shutdown_calls++;
    test_shutdown_order = test_output_events;
}

void
test_exit(int fail) {
    volatile unsigned int *test_dev = (volatile unsigned int *)0x100000;
    *test_dev = fail ? 0x3333u : 0x5555u;
    for (;;) {
        asm volatile("wfi");
    }
}
