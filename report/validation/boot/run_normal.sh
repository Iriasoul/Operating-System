#!/bin/sh
# Build the formal kernel and run it without grading macros.
#
# The run is bounded by RUN_TIMEOUT seconds. A kernel-initiated SBI shutdown
# exits QEMU with status 0; status 124 means the external timeout fired.
#
# Environment:
#   RISCV_BIN   directory holding riscv64-unknown-elf-* (default /opt/riscv/bin)
#   QEMU        real qemu-system-riscv64 (default $QEMU_REAL or /opt/qemu/bin/...)
#   QEMU_REAL   alternative name for the real emulator path
#   RUN_TIMEOUT seconds before external termination (default 30)

set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo=$(CDPATH= cd -- "$here/../../.." && pwd)
code=$repo/code
logs=$here/logs
mkdir -p "$logs"

riscv_bin=${RISCV_BIN:-/opt/riscv/bin}
qemu=${QEMU:-${QEMU_REAL:-/opt/qemu/bin/qemu-system-riscv64}}
run_timeout=${RUN_TIMEOUT:-30}

export PATH="$riscv_bin:$PATH"

if ! command -v riscv64-unknown-elf-gcc >/dev/null 2>&1; then
    echo "run_normal: riscv64-unknown-elf-gcc not found (set RISCV_BIN)" >&2
    exit 2
fi
if [ ! -x "$qemu" ]; then
    echo "run_normal: QEMU is not executable: $qemu (set QEMU or QEMU_REAL)" >&2
    exit 2
fi

echo "== tool versions =="
riscv64-unknown-elf-gcc --version | head -1
"$qemu" --version | head -1

echo "== clean build =="
if ! { make -C "$code" clean; make -C "$code"; } > "$logs/build.log" 2>&1; then
    tail -20 "$logs/build.log" >&2
    echo "run_normal: build failed, see $logs/build.log" >&2
    exit 1
fi
tail -3 "$logs/build.log"

table_addr=$(riscv64-unknown-elf-nm "$code/bin/kernel" |
    awk '$3 == "boot_page_table_sv39" { print $1 }')
entry_addr=$(riscv64-unknown-elf-nm "$code/bin/kernel" |
    awk '$3 == "kern_entry" { print $1 }')
if [ -z "$table_addr" ] || [ -z "$entry_addr" ]; then
    echo "run_normal: boot symbols not found in bin/kernel" >&2
    exit 1
fi

if ! python3 -I "$here/check_boot_pt.py" "$code/bin/ucore.img" \
        "0x$table_addr" "0x$entry_addr" > "$logs/page_table_check.log"; then
    cat "$logs/page_table_check.log" >&2
    exit 1
fi
cat "$logs/page_table_check.log"

serial_log=$logs/normal.log
rm -f "$serial_log"
set +e
timeout "$run_timeout" "$qemu" \
    -machine virt -nographic -bios default \
    -kernel "$code/bin/ucore.img" \
    -serial "file:$serial_log" -monitor none -no-reboot \
    > "$logs/qemu_stderr.log" 2>&1
run_status=$?
set -e

{
    echo "timestamp: $(date -Iseconds)"
    echo "command: timeout ${run_timeout}s $qemu -machine virt -nographic -bios default -kernel $code/bin/ucore.img -serial file:$serial_log -monitor none -no-reboot"
    echo "exit_status: $run_status"
    echo "git_head: $(git -C "$repo" rev-parse --short HEAD)"
    echo "git_status:"
    git -C "$repo" status --short
    echo "toolchain: $(riscv64-unknown-elf-gcc --version | head -1)"
    echo "qemu: $("$qemu" --version | head -1)"
    echo "sha256:"
    sha256sum "$code/kern/init/entry.S" "$code/kern/trap/trap.c" \
        "$code/bin/ucore.img"
} > "$logs/normal.meta"

ok=1
if [ "$run_status" -ne 0 ]; then
    echo "run_normal: QEMU exit status $run_status (124 means timeout)" >&2
    ok=0
fi
for pattern in 'DTB Init' 'page_init: detected physical memory base' \
        'check_alloc_page() succeeded!' '++ setup timer interrupts'; do
    if ! grep -Fq "$pattern" "$serial_log"; then
        echo "run_normal: missing '$pattern'" >&2
        ok=0
    fi
done

ticks=$(grep -c '100 ticks' "$serial_log" 2>/dev/null || true)
if [ "$ticks" -ne 10 ]; then
    echo "run_normal: expected 10 '100 ticks' lines, got $ticks" >&2
    ok=0
fi
if grep -Fq 'panic' "$serial_log"; then
    echo "run_normal: serial log contains panic" >&2
    ok=0
fi
if grep -Fq 'FAIL' "$serial_log"; then
    echo "run_normal: serial log contains FAIL" >&2
    ok=0
fi
if grep -Fq 'trapframe at' "$serial_log"; then
    echo "run_normal: serial log contains a repeated trap frame dump" >&2
    ok=0
fi

echo "run_normal: QEMU exit=$run_status ticks=$ticks"
if [ "$ok" -eq 1 ]; then
    echo "normal boot verification: PASS"
    exit 0
fi
echo "normal boot verification: FAIL" >&2
exit 1
