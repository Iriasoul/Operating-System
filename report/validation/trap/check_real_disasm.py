#!/usr/bin/env python3
"""Check the explicit real-trap encodings against the disassembly and log.

Usage:
    check_real_disasm.py <objdump-disassembly> <serial-log>
"""

import re
import sys

LINE = re.compile(r"^\s*([0-9a-f]+):\s+([0-9a-f]+)\s+(.*)$")


def parse(line):
    match = LINE.match(line)
    if not match:
        return None
    return int(match.group(1), 16), match.group(2), match.group(3).strip()


def find_encoding(lines, start_symbol, end_symbol, predicate):
    start = None
    end = len(lines)
    for i, line in enumerate(lines):
        if ("<%s>:" % start_symbol) in line:
            start = i
        elif start is not None and end_symbol and ("<%s>:" % end_symbol) in line:
            end = i
            break
    if start is None:
        raise SystemExit("symbol not found: " + start_symbol)
    for i in range(start, end):
        parsed = parse(lines[i])
        if parsed and predicate(parsed):
            return i, parsed
    raise SystemExit("encoding not found in " + start_symbol)


def next_address(lines, index):
    for i in range(index + 1, len(lines)):
        parsed = parse(lines[i])
        if parsed:
            return parsed[0]
    raise SystemExit("no instruction follows the trap encoding")


def main():
    if len(sys.argv) != 3:
        print("usage: check_real_disasm.py <disassembly> <serial-log>",
              file=sys.stderr)
        return 2

    lines = open(sys.argv[1], encoding="utf-8").read().splitlines()
    log_text = open(sys.argv[2], encoding="utf-8").read()

    specs = [
        ("16-bit breakpoint", "real_case_16bp", "real_case_32bp",
         lambda p: p[1] == "9002", 2, False),
        ("32-bit breakpoint", "real_case_32bp", "real_case_16ill",
         lambda p: p[1] == "00100073", 4, True),
        ("16-bit illegal", "real_case_16ill", "real_case_32ill",
         lambda p: p[1] == "0000" and "unimp" in p[2], 2, False),
        ("32-bit illegal", "real_case_32ill", None,
         lambda p: p[1] == "0000007b", 4, True),
    ]

    table = []
    ok = True
    for name, start, end, predicate, delta, need_2mod4 in specs:
        index, parsed = find_encoding(lines, start, end, predicate)
        address, byte_text, text = parsed
        next_addr = next_address(lines, index)
        actual_delta = next_addr - address
        mod4 = address % 4

        if actual_delta != delta:
            ok = False
        if need_2mod4 and mod4 != 2:
            ok = False
        table.append((name, address, byte_text, next_addr, actual_delta, mod4,
                      text))

    breakpoints = [int(value, 16) for value in
                   re.findall(r"Breakpoint at 0x([0-9a-fA-F]+)", log_text)]
    illegal = [int(value, 16) for value in
               re.findall(r"Illegal instruction at 0x([0-9a-fA-F]+)", log_text)]

    if breakpoints != [table[0][1], table[1][1]]:
        ok = False
    if illegal != [table[2][1], table[3][1]]:
        ok = False

    for row in table:
        print("%-18s addr=0x%08x bytes=%-8s next=0x%08x delta=%d addr_mod4=%d"
              % (row[0], row[1], row[2], row[3], row[4], row[5]))
    print("log breakpoints: " + ", ".join("0x%08x" % a for a in breakpoints))
    print("log illegal:     " + ", ".join("0x%08x" % a for a in illegal))
    print("real encoding check: " + ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
