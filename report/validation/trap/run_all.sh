#!/bin/sh
# Run every Trap validation layer and return nonzero if any layer fails.
# "ALL TRAP VALIDATION PASS" is printed only after all layers succeed.

set -u

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
fail=0

sh "$here/run_unit.sh" || fail=1
sh "$here/run_real.sh" || fail=1
sh "$here/run_regression.sh" || fail=1

if [ "$fail" -eq 0 ]; then
    echo "ALL TRAP VALIDATION PASS"
    exit 0
fi
echo "ALL TRAP VALIDATION FAIL" >&2
exit 1
