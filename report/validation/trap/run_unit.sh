#!/bin/sh
# Build and run the direct-handler and clock-boundary tests.
#
# These tests link the formal code/kern/trap/trap.c and code/libs/printfmt.c.
# Console capture, clock re-arming and shutdown are test doubles from
# common/console.c. Build artifacts live in a temporary directory; logs are
# written under report/validation/trap/logs/.

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
command -v "$CC" >/dev/null 2>&1 || {
    echo "run_unit: $CC not found (set RISCV_BIN)" >&2
    exit 2
}
[ -x "$qemu" ] || {
    echo "run_unit: QEMU not executable: $qemu (set QEMU)" >&2
    exit 2
}
mkdir -p "$logs"

build=$(mktemp -d "${TMPDIR:-/tmp}/lab3-trap-unit.XXXXXX")
trap 'rm -rf "$build"' EXIT HUP INT TERM

FLAGS="-mcmodel=medany -std=gnu99 -O2 -g -Wall -Werror -fno-builtin -nostdinc \
-ffreestanding -fno-stack-protector -msmall-data-limit=0 -mno-relax \
-march=rv64gc -mabi=lp64 \
-I$code/libs -I$code/kern/trap -I$code/kern/driver -I$code/kern/mm \
-I$code/kern/debug -I$common"

COMMON_SRCS="$common/start.S $common/console.c $common/harness.c \
$code/kern/trap/trap.c $code/kern/trap/trapentry.S \
$code/libs/printfmt.c $code/libs/string.c"

build_bin() {
    name=$1
    test_src=$2
    $CC $FLAGS -T "$common/link.ld" -Wl,--no-relax $COMMON_SRCS "$test_src" \
        -nostdlib -lgcc -o "$build/$name.elf"
    "$OBJCOPY" -O binary --strip-all "$build/$name.elf" "$build/$name.bin"
}

run_bin() {
    name=$1
    timeout_s=$2
    log=$3
    rm -f "$log"
    if timeout "$timeout_s" "$qemu" \
        -machine virt -nographic -bios default \
        -kernel "$build/$name.bin" \
        -serial "file:$log" -monitor none -no-reboot \
        > "$build/$name.stderr" 2>&1; then
        return 0
    else
        return $?
    fi
}

fail=0
echo "== building direct and clock fixtures =="
build_bin test_direct "$here/direct/test_direct.c"
build_bin test_clock "$here/direct/test_clock.c"

echo "== direct handler tests =="
if run_bin test_direct 15 "$logs/direct.log"; then
    direct_status=0
else
    direct_status=$?
fi
if [ "$direct_status" -ne 0 ]; then
    echo "run_unit: direct QEMU exit $direct_status" >&2
    fail=1
fi
if ! grep -Fq 'DIRECT TESTS PASS' "$logs/direct.log"; then
    echo "run_unit: missing DIRECT TESTS PASS" >&2
    fail=1
fi
if grep -Fq 'DIRECT TESTS FAIL' "$logs/direct.log"; then
    echo "run_unit: direct tests reported failure" >&2
    fail=1
fi
ok_count=$(grep -c '\[direct\].*: ok' "$logs/direct.log" 2>/dev/null || true)
if [ "$ok_count" -ne 21 ]; then
    echo "run_unit: expected 21 direct case results, got $ok_count" >&2
    fail=1
fi

echo "== clock boundary test =="
if run_bin test_clock 25 "$logs/clock.log"; then
    clock_status=0
else
    clock_status=$?
fi
if [ "$clock_status" -ne 0 ]; then
    echo "run_unit: clock QEMU exit $clock_status" >&2
    fail=1
fi
if ! grep -Fq 'CLOCK BOUNDARY PASS' "$logs/clock.log"; then
    echo "run_unit: missing CLOCK BOUNDARY PASS" >&2
    fail=1
fi
for pattern in 'tick=1000 stats=10 shutdown=1 order=1/1' \
    '[clock] checked 1000 interrupts'; do
    if ! grep -Fq "$pattern" "$logs/clock.log"; then
        echo "run_unit: missing clock evidence: $pattern" >&2
        fail=1
    fi
done
clock_ticks=$(grep -c '\[clock\] tick=' "$logs/clock.log" 2>/dev/null || true)
if [ "$clock_ticks" -ne 10 ]; then
    echo "run_unit: expected 10 clock boundary markers, got $clock_ticks" >&2
    fail=1
fi
if grep -Fq 'FAIL' "$logs/clock.log"; then
    echo "run_unit: clock log contains FAIL" >&2
    fail=1
fi

{
    echo "timestamp: $(date -Iseconds)"
    echo "git_head: $(git -C "$repo" rev-parse --short HEAD)"
    echo "git_status:"
    git -C "$repo" status --short
    echo "toolchain: $("$CC" --version | head -1)"
    echo "qemu: $("$qemu" --version | head -1)"
    echo "direct_qemu_exit: $direct_status"
    echo "clock_qemu_exit: $clock_status"
    echo "direct_ok_cases: $ok_count"
    echo "clock_markers: $clock_ticks"
    echo "sha256:"
    sha256sum "$code/kern/trap/trap.c" "$code/kern/init/entry.S" \
        "$build/test_direct.elf" "$build/test_clock.elf"
} > "$logs/unit.meta"

if [ "$fail" -eq 0 ]; then
    echo "UNIT TESTS PASS"
    exit 0
fi
echo "UNIT TESTS FAIL" >&2
exit 1
