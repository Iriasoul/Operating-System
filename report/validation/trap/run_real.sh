#!/bin/sh
# Build and run the real Trap-return tests.
#
# The test uses formal code/kern/trap/trapentry.S and code/kern/trap/trap.c.
# Only the test entry/linker script live in this fixture; formal init.c,
# trapentry.S, trap.c and the Makefile are not modified.

set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo=$(CDPATH= cd -- "$here/../../.." && pwd)
code=$repo/code
logs=$here/logs
common=$here/common

riscv_bin=${RISCV_BIN:-/opt/riscv/bin}
qemu=${QEMU:-${QEMU_REAL:-/opt/qemu/bin/qemu-system-riscv64}}
export PATH="$riscv_bin:$PATH"

CC=riscv64-unknown-elf-gcc
OBJCOPY=riscv64-unknown-elf-objcopy
OBJDUMP=riscv64-unknown-elf-objdump
command -v "$CC" >/dev/null 2>&1 || {
    echo "run_real: $CC not found (set RISCV_BIN)" >&2
    exit 2
}
[ -x "$qemu" ] || {
    echo "run_real: QEMU not executable: $qemu (set QEMU)" >&2
    exit 2
}
mkdir -p "$logs"

build=$(mktemp -d "${TMPDIR:-/tmp}/lab3-trap-real.XXXXXX")
trap 'rm -rf "$build"' EXIT HUP INT TERM

FLAGS="-mcmodel=medany -std=gnu99 -O2 -g -Wall -Werror -fno-builtin -nostdinc \
-ffreestanding -fno-stack-protector -msmall-data-limit=0 -mno-relax \
-march=rv64gc -mabi=lp64 \
-I$code/libs -I$code/kern/trap -I$code/kern/driver -I$code/kern/mm \
-I$code/kern/debug -I$common"

$CC $FLAGS -T "$common/link.ld" -Wl,--no-relax \
    "$common/start.S" "$common/console.c" "$common/harness.c" \
    "$here/real/test_real.c" "$here/real/real_cases.S" \
    "$code/kern/trap/trap.c" "$code/kern/trap/trapentry.S" \
    "$code/libs/printfmt.c" "$code/libs/string.c" \
    -nostdlib -lgcc -o "$build/test_real.elf"
"$OBJCOPY" -O binary --strip-all "$build/test_real.elf" "$build/test_real.bin"
"$OBJDUMP" -d "$build/test_real.elf" > "$logs/real_disasm.txt"

rm -f "$logs/real.log"
if timeout 15 "$qemu" \
    -machine virt -nographic -bios default \
    -kernel "$build/test_real.bin" \
    -serial "file:$logs/real.log" -monitor none -no-reboot \
    > "$build/test_real.stderr" 2>&1; then
    real_status=0
else
    real_status=$?
fi

fail=0
if [ "$real_status" -ne 0 ]; then
    echo "run_real: QEMU exit $real_status" >&2
    fail=1
fi
if ! grep -Fq 'REAL TRAP TESTS PASS' "$logs/real.log"; then
    echo "run_real: missing REAL TRAP TESTS PASS" >&2
    fail=1
fi
if grep -Fq 'FAIL' "$logs/real.log"; then
    echo "run_real: real test log contains FAIL" >&2
    fail=1
fi
real_ok=$(grep -c '\[real\].*: ok' "$logs/real.log" 2>/dev/null || true)
if [ "$real_ok" -ne 4 ]; then
    echo "run_real: expected 4 real case results, got $real_ok" >&2
    fail=1
fi

if ! python3 -I "$here/check_real_disasm.py" \
        "$logs/real_disasm.txt" "$logs/real.log" \
        > "$logs/real_encoding_check.txt"; then
    cat "$logs/real_encoding_check.txt" >&2
    echo "run_real: encoding check failed" >&2
    fail=1
fi
cat "$logs/real_encoding_check.txt"

{
    echo "timestamp: $(date -Iseconds)"
    echo "git_head: $(git -C "$repo" rev-parse --short HEAD)"
    echo "git_status:"
    git -C "$repo" status --short
    echo "toolchain: $("$CC" --version | head -1)"
    echo "qemu: $("$qemu" --version | head -1)"
    echo "real_qemu_exit: $real_status"
    echo "real_ok_cases: $real_ok"
    echo "sha256:"
    sha256sum "$code/kern/trap/trap.c" "$code/kern/trap/trapentry.S" \
        "$build/test_real.elf"
} > "$logs/real.meta"

if [ "$fail" -eq 0 ]; then
    echo "REAL TRAP RETURN TESTS PASS"
    exit 0
fi
echo "REAL TRAP RETURN TESTS FAIL" >&2
exit 1
