#!/bin/sh
# Regression: formal-source normal boot and official Lab3 grading.
#
# The normal boot mirrors report/validation/boot/run_normal.sh but writes to
# this round's log directory, leaving the earlier boot evidence untouched.
# Grading uses the existing boot QEMU adapter; it is not modified.

set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo=$(CDPATH= cd -- "$here/../../.." && pwd)
code=$repo/code
logs=$here/logs/regression
adapter=$repo/report/validation/boot/qemu-adapter
mkdir -p "$logs"

riscv_bin=${RISCV_BIN:-/opt/riscv/bin}
qemu_real=${QEMU_REAL:-/opt/qemu/bin/qemu-system-riscv64}
export PATH="$riscv_bin:$PATH"

command -v riscv64-unknown-elf-gcc >/dev/null 2>&1 || {
    echo "run_regression: riscv64-unknown-elf-gcc not found" >&2
    exit 2
}
[ -x "$qemu_real" ] || {
    echo "run_regression: QEMU not executable: $qemu_real" >&2
    exit 2
}
[ -x "$adapter/qemu-system-riscv64" ] || {
    echo "run_regression: adapter missing: $adapter/qemu-system-riscv64" >&2
    exit 2
}

fail=0

echo "== normal boot from formal source =="
if ! { make -C "$code" clean; make -C "$code"; } > "$logs/build.log" 2>&1; then
    tail -20 "$logs/build.log" >&2
    echo "run_regression: build failed" >&2
    fail=1
fi

rm -f "$logs/normal.log"
if timeout 30 "$qemu_real" \
    -machine virt -nographic -bios default \
    -kernel "$code/bin/ucore.img" \
    -serial "file:$logs/normal.log" -monitor none -no-reboot \
    > "$logs/normal.stderr" 2>&1; then
    normal_status=0
else
    normal_status=$?
fi
if [ "$normal_status" -ne 0 ]; then
    echo "run_regression: normal QEMU exit $normal_status" >&2
    fail=1
fi
for pattern in 'DTB Init' 'check_alloc_page() succeeded!' \
    '++ setup timer interrupts'; do
    if ! grep -Fq "$pattern" "$logs/normal.log"; then
        echo "run_regression: normal log missing: $pattern" >&2
        fail=1
    fi
done
normal_ticks=$(grep -c '100 ticks' "$logs/normal.log" 2>/dev/null || true)
if [ "$normal_ticks" -ne 10 ]; then
    echo "run_regression: expected 10 normal tick lines, got $normal_ticks" >&2
    fail=1
fi
if grep -Fq 'panic' "$logs/normal.log" || grep -Fq 'FAIL' "$logs/normal.log" ||
    grep -Fq 'trapframe at' "$logs/normal.log"; then
    echo "run_regression: normal log contains panic/FAIL/trapframe" >&2
    fail=1
fi

echo "== official make grade with the existing adapter =="
export QEMU_REAL="$qemu_real"
export PATH="$adapter:$PATH"
set +e
( cd "$code" && make grade ) > "$logs/make_grade.out" 2>&1
make_status=$?
set -e
tail -4 "$logs/make_grade.out"

echo "== official inject.py + grade.sh for per-case logs =="
tmp=$(mktemp -d "${TMPDIR:-/tmp}/lab3-trap-regression.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
tar -C "$code" --exclude=obj --exclude=bin -cf - . | tar -C "$tmp" -xf -

set +e
( cd "$tmp" && python3 -I tools/inject.py . && sh tools/grade.sh ) \
    > "$logs/grade_direct.out" 2>&1
direct_status=$?
set -e
tail -4 "$logs/grade_direct.out"

for f in "$tmp"/.qemu.*.out; do
    [ -e "$f" ] || continue
    cp "$f" "$logs/"
done

if [ "$make_status" -ne 0 ] || [ "$direct_status" -ne 0 ]; then
    echo "run_regression: make=$make_status direct=$direct_status" >&2
    fail=1
fi
for out in "$logs/make_grade.out" "$logs/grade_direct.out"; do
    if ! grep -Fq 'Total Score: 100/100' "$out"; then
        echo "run_regression: missing 100/100 in $out" >&2
        fail=1
    fi
done
for f in "$logs"/.qemu.*.out; do
    [ -e "$f" ] || continue
    if grep -Fq 'FAIL' "$f" || grep -Fq 'panic' "$f"; then
        echo "run_regression: failure in $f" >&2
        fail=1
    fi
done

{
    echo "timestamp: $(date -Iseconds)"
    echo "git_head: $(git -C "$repo" rev-parse --short HEAD)"
    echo "git_status:"
    git -C "$repo" status --short
    echo "toolchain: $(riscv64-unknown-elf-gcc --version | head -1)"
    echo "qemu_real: $qemu_real"
    "$qemu_real" --version | head -1
    echo "adapter: $adapter/qemu-system-riscv64"
    echo "adapter_rule: -device loader,file=<path>,addr=0x80200000 -> -kernel <path>"
    echo "normal_qemu_exit: $normal_status"
    echo "normal_ticks: $normal_ticks"
    echo "make_grade_exit: $make_status"
    echo "direct_grade_exit: $direct_status"
    echo "sha256:"
    sha256sum "$code/kern/trap/trap.c" "$code/kern/init/entry.S"
} > "$logs/regression.meta"

if [ "$fail" -eq 0 ]; then
    echo "REGRESSION PASS: normal boot and official grade 100/100"
    exit 0
fi
echo "REGRESSION FAIL" >&2
exit 1
