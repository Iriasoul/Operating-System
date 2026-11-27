#include <stdio.h>
#include <ulib.h>

int
main(void) {
    int value;
    // Exercise the RISC-V instruction directly because C division by zero is undefined.
    asm volatile ("div %0, %1, zero" : "=r"(value) : "r"(1));
    cprintf("value is %d.\n", value);
    assert(value == -1);
    return 0;
}

