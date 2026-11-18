#!/bin/sh
set -eu

testdir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$testdir/../.." && pwd)
build=$(mktemp -d "${TMPDIR:-/tmp}/lab4-module2.XXXXXX")
trap 'rm -f "$build/test"; rmdir "$build"' EXIT HUP INT TERM

gcc -std=gnu99 -O1 -g -D__riscv_xlen=64 -fno-builtin -nostdinc \
    -Wall -Wextra -Wno-unused-parameter -Wno-unused-function \
    -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$testdir/stubs" -I"$root/libs" -I"$root/kern/process" \
    -I"$root/kern/debug" -I"$root/kern/driver" -I"$root/kern/trap" \
    -I"$root/kern/mm" -I"$root/kern/sync" -I"$root/kern/schedule" \
    "$testdir/test.c" "$root/libs/hash.c" -o "$build/test"

ASAN_OPTIONS=detect_leaks=1 "$build/test"
