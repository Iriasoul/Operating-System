/*
 * Explicit Trap-return cases. Each function executes one fixed encoding and
 * then checks that GPRs, sp and ra survived the real trapentry.S + trap.c
 * round trip. The C caller sees only the 0/1 return value in a0.
 */

    .text

/* 16-bit breakpoint: c.ebreak (0x9002), next instruction at +2. */
    .p2align 2
    .globl real_case_16bp
real_case_16bp:
    addi sp, sp, -48
    sd ra, 0(sp)
    sd s1, 8(sp)
    sd s2, 16(sp)
    sd s3, 24(sp)
    sd s4, 32(sp)
    li s1, 0x1111
    li s2, 0x2222
    li s3, 0x3333
    mv s4, sp
    li t0, 0x4444
    li t1, 0x5555
    li t2, 0x6666

    .2byte 0x9002

    li a1, 0x1111
    bne s1, a1, .Lfail_16bp
    li a1, 0x2222
    bne s2, a1, .Lfail_16bp
    li a1, 0x3333
    bne s3, a1, .Lfail_16bp
    li a1, 0x4444
    bne t0, a1, .Lfail_16bp
    li a1, 0x5555
    bne t1, a1, .Lfail_16bp
    li a1, 0x6666
    bne t2, a1, .Lfail_16bp
    bne sp, s4, .Lfail_16bp
    ld a1, 0(sp)
    bne ra, a1, .Lfail_16bp
    li a0, 0
    j .Lret_16bp
.Lfail_16bp:
    li a0, 1
.Lret_16bp:
    ld ra, 0(sp)
    ld s1, 8(sp)
    ld s2, 16(sp)
    ld s3, 24(sp)
    ld s4, 32(sp)
    addi sp, sp, 48
    ret

/* 32-bit breakpoint: 0x00100073 at an address that is 2 mod 4. */
    .p2align 2
    .globl real_case_32bp
real_case_32bp:
    addi sp, sp, -48
    sd ra, 0(sp)
    sd s1, 8(sp)
    sd s2, 16(sp)
    sd s3, 24(sp)
    sd s4, 32(sp)
    li s1, 0x1111
    li s2, 0x2222
    li s3, 0x3333
    mv s4, sp
    li t0, 0x4444
    li t1, 0x5555
    li t2, 0x6666

    .balign 4
    .2byte 0x0001          /* c.nop so the next 32-bit word is 2 mod 4 */
    .4byte 0x00100073      /* ebreak */

    li a1, 0x1111
    bne s1, a1, .Lfail_32bp
    li a1, 0x2222
    bne s2, a1, .Lfail_32bp
    li a1, 0x3333
    bne s3, a1, .Lfail_32bp
    li a1, 0x4444
    bne t0, a1, .Lfail_32bp
    li a1, 0x5555
    bne t1, a1, .Lfail_32bp
    li a1, 0x6666
    bne t2, a1, .Lfail_32bp
    bne sp, s4, .Lfail_32bp
    ld a1, 0(sp)
    bne ra, a1, .Lfail_32bp
    li a0, 0
    j .Lret_32bp
.Lfail_32bp:
    li a0, 1
.Lret_32bp:
    ld ra, 0(sp)
    ld s1, 8(sp)
    ld s2, 16(sp)
    ld s3, 24(sp)
    ld s4, 32(sp)
    addi sp, sp, 48
    ret

/* 16-bit illegal instruction: all-zero encoding. */
    .p2align 2
    .globl real_case_16ill
real_case_16ill:
    addi sp, sp, -48
    sd ra, 0(sp)
    sd s1, 8(sp)
    sd s2, 16(sp)
    sd s3, 24(sp)
    sd s4, 32(sp)
    li s1, 0x1111
    li s2, 0x2222
    li s3, 0x3333
    mv s4, sp
    li t0, 0x4444
    li t1, 0x5555
    li t2, 0x6666

    .2byte 0x0000

    li a1, 0x1111
    bne s1, a1, .Lfail_16ill
    li a1, 0x2222
    bne s2, a1, .Lfail_16ill
    li a1, 0x3333
    bne s3, a1, .Lfail_16ill
    li a1, 0x4444
    bne t0, a1, .Lfail_16ill
    li a1, 0x5555
    bne t1, a1, .Lfail_16ill
    li a1, 0x6666
    bne t2, a1, .Lfail_16ill
    bne sp, s4, .Lfail_16ill
    ld a1, 0(sp)
    bne ra, a1, .Lfail_16ill
    li a0, 0
    j .Lret_16ill
.Lfail_16ill:
    li a0, 1
.Lret_16ill:
    ld ra, 0(sp)
    ld s1, 8(sp)
    ld s2, 16(sp)
    ld s3, 24(sp)
    ld s4, 32(sp)
    addi sp, sp, 48
    ret

/* 32-bit illegal instruction: 0x0000007b at an address that is 2 mod 4. */
    .p2align 2
    .globl real_case_32ill
real_case_32ill:
    addi sp, sp, -48
    sd ra, 0(sp)
    sd s1, 8(sp)
    sd s2, 16(sp)
    sd s3, 24(sp)
    sd s4, 32(sp)
    li s1, 0x1111
    li s2, 0x2222
    li s3, 0x3333
    mv s4, sp
    li t0, 0x4444
    li t1, 0x5555
    li t2, 0x6666

    .balign 4
    .2byte 0x0001          /* c.nop so the next 32-bit word is 2 mod 4 */
    .4byte 0x0000007b      /* custom invalid 32-bit encoding */

    li a1, 0x1111
    bne s1, a1, .Lfail_32ill
    li a1, 0x2222
    bne s2, a1, .Lfail_32ill
    li a1, 0x3333
    bne s3, a1, .Lfail_32ill
    li a1, 0x4444
    bne t0, a1, .Lfail_32ill
    li a1, 0x5555
    bne t1, a1, .Lfail_32ill
    li a1, 0x6666
    bne t2, a1, .Lfail_32ill
    bne sp, s4, .Lfail_32ill
    ld a1, 0(sp)
    bne ra, a1, .Lfail_32ill
    li a0, 0
    j .Lret_32ill
.Lfail_32ill:
    li a0, 1
.Lret_32ill:
    ld ra, 0(sp)
    ld s1, 8(sp)
    ld s2, 16(sp)
    ld s3, 24(sp)
    ld s4, 32(sp)
    addi sp, sp, 48
    ret
