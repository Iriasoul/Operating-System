    .section .text.entry
    .globl _start
_start:
    # Clear .bss, including the test stack and trap.c's static counters.
    la t0, __bss_start
    la t1, __bss_end
1:
    bgeu t0, t1, 2f
    sd zero, 0(t0)
    addi t0, t0, 8
    j 1b
2:
    la sp, __stack_top
    # sret from the trap entry returns to S-mode.
    li t0, 0x2
    csrw sstatus, t0
    call test_main
3:
    j 3b
