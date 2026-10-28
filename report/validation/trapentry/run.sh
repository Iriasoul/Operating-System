#!/bin/sh
# Run only the module-one harness against the actual trapentry.S.
set -eu

fixture_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$fixture_dir/../../.." && pwd)
build_dir="$repo_dir/code/obj/trapentry-recheck"
cc=${TRAPENTRY_CC:-/opt/riscv-elf-toolchains/bin/riscv64-unknown-elf-gcc}
objcopy=${TRAPENTRY_OBJCOPY:-/opt/riscv-elf-toolchains/bin/riscv64-unknown-elf-objcopy}
qemu=${TRAPENTRY_QEMU:-/home/compiler/.local/opt/qemu-4.1.1/bin/qemu-system-riscv64}

mkdir -p "$build_dir"
"$cc" -std=gnu99 -Dsbadaddr=stval \
    -march=rv64imac_zicsr_zifencei -mabi=lp64 -mcmodel=medany \
    -mno-relax -msmall-data-limit=0 -O2 -g -ffreestanding -fno-builtin \
    -fno-stack-protector -nostdinc -nostdlib -Wall -Wextra -Werror \
    -I"$repo_dir/code/libs" -I"$repo_dir/code/kern/trap" \
    -Wl,-T,"$fixture_dir/link.ld" -Wl,--no-relax \
    "$fixture_dir/start.S" "$repo_dir/code/kern/trap/trapentry.S" \
    "$fixture_dir/check.c" -o "$build_dir/test.elf"
"$objcopy" -O binary "$build_dir/test.elf" "$build_dir/test.bin"

if timeout 10s "$qemu" -machine virt -m 128M -smp 1 -nographic \
    -bios default -device "loader,file=$build_dir/test.bin,addr=0x80200000" \
    -monitor none -no-reboot > "$build_dir/qemu.log" 2>&1; then
    cat "$build_dir/qemu.log"
else
    run_status=$?
    cat "$build_dir/qemu.log"
    printf 'Module-one QEMU run failed (exit %s).\n' "$run_status" >&2
    exit "$run_status"
fi

if grep -F 'MODULE1 FAIL' "$build_dir/qemu.log"; then
    exit 1
fi
grep -F 'MODULE1 PASS: 16-bit breakpoint, all GPRs, sp, CSR, return' \
    "$build_dir/qemu.log" >/dev/null
grep -F 'MODULE1 PASS: 32-bit breakpoint, all GPRs, sp, CSR, return' \
    "$build_dir/qemu.log" >/dev/null
grep -F 'MODULE1 ALL PASS' "$build_dir/qemu.log" >/dev/null
