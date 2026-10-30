#!/bin/sh
# Run the official Lab3 grading flow against the formal source.
#
# Two entries are recorded:
#   1. "make grade" in code/, with the QEMU adapter on PATH. This is the
#      official target; it deletes its own temporary directory and per-case
#      logs.
#   2. The same official tools/inject.py and tools/grade.sh invoked directly
#      in a source-identical temporary copy, without any kernel patch, so the
#      per-case .qemu.*.out logs can be archived.
#
# The adapter only rewrites
#   -device loader,file=<path>,addr=0x80200000  ->  -kernel <path>
# and rejects other forms; see qemu-adapter/qemu-system-riscv64.

set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo=$(CDPATH= cd -- "$here/../../.." && pwd)
code=$repo/code
logs=$here/logs/grade
adapter=$here/qemu-adapter
mkdir -p "$logs"

riscv_bin=${RISCV_BIN:-/opt/riscv/bin}
qemu_real=${QEMU_REAL:-/opt/qemu/bin/qemu-system-riscv64}

export QEMU_REAL="$qemu_real"
export PATH="$adapter:$riscv_bin:$PATH"

if ! command -v riscv64-unknown-elf-gcc >/dev/null 2>&1; then
    echo "run_grade: riscv64-unknown-elf-gcc not found (set RISCV_BIN)" >&2
    exit 2
fi
if [ ! -x "$qemu_real" ]; then
    echo "run_grade: real QEMU is not executable: $qemu_real" >&2
    exit 2
fi
if [ ! -x "$adapter/qemu-system-riscv64" ]; then
    echo "run_grade: adapter not executable: $adapter/qemu-system-riscv64" >&2
    exit 2
fi

echo "== tool versions =="
riscv64-unknown-elf-gcc --version | head -1
"$qemu_real" --version | head -1

echo "== make grade (official target, adapter on PATH) =="
set +e
( cd "$code" && make grade ) > "$logs/make_grade.out" 2>&1
make_status=$?
set -e
tail -5 "$logs/make_grade.out"

echo "== official inject.py + grade.sh in a source-identical copy =="
tmp=$(mktemp -d "${TMPDIR:-/tmp}/lab3-grade-logs.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
tar -C "$code" --exclude=obj --exclude=bin -cf - . | tar -C "$tmp" -xf -

set +e
( cd "$tmp" && python3 -I tools/inject.py . && sh tools/grade.sh ) \
    > "$logs/grade_direct.out" 2>&1
direct_status=$?
set -e
tail -6 "$logs/grade_direct.out"

for f in "$tmp"/.qemu.*.out; do
    [ -e "$f" ] || continue
    cp "$f" "$logs/"
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
    echo "make_grade_exit: $make_status"
    echo "direct_grade_exit: $direct_status"
    echo "sha256:"
    sha256sum "$code/kern/init/entry.S" "$code/kern/trap/trap.c"
} > "$logs/grade.meta"

ok=1
for out in "$logs/make_grade.out" "$logs/grade_direct.out"; do
    if ! grep -Fq 'Total Score: 100/100' "$out"; then
        echo "run_grade: missing 'Total Score: 100/100' in $out" >&2
        ok=0
    fi
done
if [ "$make_status" -ne 0 ] || [ "$direct_status" -ne 0 ]; then
    echo "run_grade: make=$make_status direct=$direct_status" >&2
    ok=0
fi

for f in "$logs"/.qemu.*.out; do
    [ -e "$f" ] || continue
    if grep -Fq 'FAIL' "$f"; then
        echo "run_grade: FAIL in $f" >&2
        ok=0
    fi
    if grep -Fq 'panic' "$f"; then
        echo "run_grade: panic in $f" >&2
        ok=0
    fi
done

count_match() {
    file=$1
    pattern=$2
    expected=$3
    actual=$(grep -c "$pattern" "$file" 2>/dev/null || true)
    if [ "$actual" -ne "$expected" ]; then
        echo "run_grade: $file: '$pattern' count $actual, expected $expected" >&2
        ok=0
    fi
}

context_log=$logs/.qemu.GRADE_LAB3_CONTEXT.out
illegal_log=$logs/.qemu.GRADE_LAB3_ILLEGAL.out
timer_log=$logs/.qemu.GRADE_LAB3_TIMER.out
count_match "$context_log" 'Breakpoint at' 1
count_match "$illegal_log" 'Illegal instruction at' 1
count_match "$illegal_log" 'Supervisor software interrupt' 1
count_match "$illegal_log" 'Unknown interrupt: cause 15' 1
count_match "$illegal_log" 'cause    0x00000014' 1
count_match "$timer_log" '100 ticks' 10
if ! grep -Fq 'ticks=200' "$timer_log"; then
    echo "run_grade: timer log does not contain ticks=200" >&2
    ok=0
fi

echo "run_grade: make_grade_exit=$make_status direct_grade_exit=$direct_status"
if [ "$ok" -eq 1 ]; then
    echo "official grading verification: PASS (100/100)"
    exit 0
fi
echo "official grading verification: FAIL" >&2
exit 1
