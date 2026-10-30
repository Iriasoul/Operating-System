#include <defs.h>
#include <string.h>
#include <trap.h>

#include "harness.h"

typedef char frame_size_check[
    (sizeof(struct trapframe) == 36 * sizeof(uintptr_t)) ? 1 : -1];

void
frame_fill(struct trapframe *tf, uintptr_t epc, uintptr_t cause) {
    uintptr_t *words = (uintptr_t *)tf;
    for (size_t i = 0; i < sizeof(*tf) / sizeof(uintptr_t); i++) {
        words[i] = 0x1000 + i;
    }
    tf->gpr.zero = 0;
    tf->status = 0x0000000000001234;
    tf->epc = epc;
    tf->badvaddr = 0x00000000deadbeef;
    tf->cause = cause;
}

int
frame_equal(const struct trapframe *a, const struct trapframe *b) {
    const uintptr_t *wa = (const uintptr_t *)a;
    const uintptr_t *wb = (const uintptr_t *)b;
    for (size_t i = 0; i < sizeof(*a) / sizeof(uintptr_t); i++) {
        if (wa[i] != wb[i]) {
            return 0;
        }
    }
    return 1;
}

int
capture_starts_with(const char *prefix) {
    size_t len = strlen(prefix);
    if (capture_length() < len) {
        return 0;
    }
    return memcmp(capture_text(), prefix, len) == 0;
}

int
capture_contains(const char *needle) {
    size_t len = strlen(needle);
    const char *text = capture_text();
    size_t text_len = capture_length();
    if (len == 0) {
        return 1;
    }
    for (size_t i = 0; i + len <= text_len; i++) {
        if (memcmp(text + i, needle, len) == 0) {
            return 1;
        }
    }
    return 0;
}
