#include <trap.h>
#include <riscv.h>

extern char input_stack_top[], output_stack_top[], break16[], break32[];
extern void round32(void);
static volatile unsigned int round_number = 0;
static volatile unsigned int failure = 0;

typedef char check_frame_size[(sizeof(struct trapframe) == 36 * 8) ? 1 : -1];

static void putchar_sbi(char c) {
    register unsigned long a0 asm("a0") = (unsigned char)c;
    register unsigned long a7 asm("a7") = 1;
    asm volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
}

static void puts_sbi(const char *s) {
    while (*s) putchar_sbi(*s++);
}

static void finish(void) __attribute__((noreturn));
static void finish(void) {
    register unsigned long a0 asm("a0") = 0;
    register unsigned long a7 asm("a7") = 8;
    asm volatile("ecall" : "+r"(a0) : "r"(a7) : "memory");
    for (;;) asm volatile("wfi");
}

/* A test fixture for module one, independent of the unfinished handlers. */
void trap(struct trapframe *tf) {
    uintptr_t *regs = (uintptr_t *)&tf->gpr;
    unsigned int i;
    uintptr_t expected_epc = (uintptr_t)(round_number ? break32 : break16);
    if ((uintptr_t)tf != (uintptr_t)input_stack_top - sizeof(*tf)) failure = 1;
    if ((uintptr_t)tf & 15) failure = 2;
    for (i = 0; i < 32; i++) {
        uintptr_t expected = i == 0 ? 0 : i == 2 ? (uintptr_t)input_stack_top : 0x100 + i;
        if (regs[i] != expected) failure = 100 + i;
    }
    if (tf->cause != 3 || tf->epc != expected_epc) failure = 3;
    if ((tf->status & (SSTATUS_SPP | SSTATUS_SPIE | SSTATUS_SIE)) !=
        (SSTATUS_SPP | SSTATUS_SPIE)) failure = 4;
    if (read_csr(sscratch) != 0) failure = 5;

    /* Change every saved GPR, including sp, to prove restoration from the frame. */
    for (i = 1; i < 32; i++) regs[i] = 0x200 + i;
    tf->gpr.sp = (uintptr_t)output_stack_top;
    tf->epc += round_number ? 4 : 2;
    tf->status |= 0x40000; /* SUM: an observable writable sstatus bit. */
    if (round_number) tf->status &= ~SSTATUS_SPIE;
}

void verify_after(uintptr_t *regs, uintptr_t status, uintptr_t epc, uintptr_t scratch) {
    unsigned int i;
    uintptr_t expected_epc = (uintptr_t)(round_number ? break32 + 4 : break16 + 2);
    for (i = 0; i < 32; i++) {
        uintptr_t expected = i == 0 ? 0 : i == 2 ? (uintptr_t)output_stack_top : 0x200 + i;
        if (regs[i] != expected) failure = 200 + i;
    }
    if (epc != expected_epc || scratch != 0) failure = 6;
    if (!(status & 0x40000) || (status & SSTATUS_SPP)) failure = 7;
    if (!!(status & SSTATUS_SIE) != !round_number) failure = 8;
    /* QEMU 4.1.1 clears SPIE in sret; verify restored SIE above instead. */
    if (failure) {
        puts_sbi("MODULE1 FAIL code=");
        putchar_sbi('0' + failure / 100);
        putchar_sbi('0' + failure / 10 % 10);
        putchar_sbi('0' + failure % 10);
        puts_sbi(" status=0x");
        for (i = 0; i < 16; i++) putchar_sbi("0123456789abcdef"[(status >> (60 - i * 4)) & 15]);
        puts_sbi("\n");
        finish();
    }
    puts_sbi(round_number ? "MODULE1 PASS: 32-bit breakpoint, all GPRs, sp, CSR, return\n"
                          : "MODULE1 PASS: 16-bit breakpoint, all GPRs, sp, CSR, return\n");
    if (!round_number) {
        round_number = 1;
        round32();
    }
    puts_sbi("MODULE1 ALL PASS\n");
    finish();
}
