#ifndef LAB4_TEST_RISCV_H
#define LAB4_TEST_RISCV_H

#include "../../../libs/riscv.h"

// Host tests model CSR operations; real RISC-V assembly runs under QEMU.
extern uintptr_t test_sstatus;
#undef read_csr
#define read_csr(reg) test_##reg
void test_lsatp(uintptr_t pgdir);
#define lsatp(pgdir) test_lsatp(pgdir)

#endif
