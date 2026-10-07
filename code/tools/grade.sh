#!/bin/sh
# Local lab1 checks; the supplied archive did not contain this script.
set -eu
cd "$(dirname "$0")/.."
make --no-print-directory
"${PYTHON:-python3}" tools/verify_lab1.py --mode all --qemu "${QEMU:-qemu-system-riscv64}" --gdb "${GDB:-riscv64-unknown-elf-gdb}"
