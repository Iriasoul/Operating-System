    .section .text.entry
    .globl _start
_start:
    la sp, input_stack_top
    la t0, __alltraps
    csrw stvec, t0
    csrw sie, zero
    csrw sscratch, zero
    j round16

    .macro SEED
    la sp, input_stack_top
    li t0, 0x2
    csrw sstatus, t0
    li x1, 0x101
    li x3, 0x103
    li x4, 0x104
    li x5, 0x105
    li x6, 0x106
    li x7, 0x107
    li x8, 0x108
    li x9, 0x109
    li x10, 0x10a
    li x11, 0x10b
    li x12, 0x10c
    li x13, 0x10d
    li x14, 0x10e
    li x15, 0x10f
    li x16, 0x110
    li x17, 0x111
    li x18, 0x112
    li x19, 0x113
    li x20, 0x114
    li x21, 0x115
    li x22, 0x116
    li x23, 0x117
    li x24, 0x118
    li x25, 0x119
    li x26, 0x11a
    li x27, 0x11b
    li x28, 0x11c
    li x29, 0x11d
    li x30, 0x11e
    li x31, 0x11f
    .endm

round16:
    SEED
    .globl break16
break16:
    .2byte 0x9002
    j capture

    .globl round32
round32:
    SEED
    .globl break32
break32:
    .4byte 0x00100073
    j capture

capture:
    csrw sscratch, sp
    addi sp, sp, -256
    sd x0, 0*8(sp)
    sd x1, 1*8(sp)
    sd x3, 3*8(sp)
    sd x4, 4*8(sp)
    sd x5, 5*8(sp)
    sd x6, 6*8(sp)
    sd x7, 7*8(sp)
    sd x8, 8*8(sp)
    sd x9, 9*8(sp)
    sd x10, 10*8(sp)
    sd x11, 11*8(sp)
    sd x12, 12*8(sp)
    sd x13, 13*8(sp)
    sd x14, 14*8(sp)
    sd x15, 15*8(sp)
    sd x16, 16*8(sp)
    sd x17, 17*8(sp)
    sd x18, 18*8(sp)
    sd x19, 19*8(sp)
    sd x20, 20*8(sp)
    sd x21, 21*8(sp)
    sd x22, 22*8(sp)
    sd x23, 23*8(sp)
    sd x24, 24*8(sp)
    sd x25, 25*8(sp)
    sd x26, 26*8(sp)
    sd x27, 27*8(sp)
    sd x28, 28*8(sp)
    sd x29, 29*8(sp)
    sd x30, 30*8(sp)
    sd x31, 31*8(sp)
    csrrw t0, sscratch, zero
    sd t0, 2*8(sp)
    mv a0, sp
    csrr a1, sstatus
    csrr a2, sepc
    csrr a3, sscratch
    call verify_after
1:  j 1b

    .section .bss
    .balign 16
    .space 16384
    .globl input_stack_top
input_stack_top:
    .space 16384
    .globl output_stack_top
output_stack_top:
