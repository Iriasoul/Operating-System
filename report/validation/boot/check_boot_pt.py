#!/usr/bin/env python3
"""Check the Sv39 boot root page table embedded in bin/ucore.img.

Usage:
    check_boot_pt.py <ucore.img> <boot_page_table_sv39> <image_base>

The flat image starts at the virtual address of kern_entry; the physical load
address is handled by QEMU. Only the two leaf entries required by the boot
mapping may be valid.
"""

import struct
import sys

EXPECTED = {
    2: 0x200000CF,    # VA [0x80000000, 0xc0000000) -> PA [0x80000000, 0xc0000000)
    511: 0x200000CF,  # VA [0xffffffffc0000000, 2^64) -> PA [0x80000000, 0xc0000000)
}


def main():
    if len(sys.argv) != 4:
        print("usage: check_boot_pt.py <ucore.img> <table_addr> <image_base>",
              file=sys.stderr)
        return 2

    image, table_s, base_s = sys.argv[1], sys.argv[2], sys.argv[3]
    table_addr = int(table_s, 16)
    base_addr = int(base_s, 16)
    offset = table_addr - base_addr
    if offset < 0:
        print("table address precedes the image base", file=sys.stderr)
        return 2

    with open(image, "rb") as f:
        data = f.read()
    raw = data[offset:offset + 4096]
    if len(raw) != 4096:
        print("image is too short for a 4096-byte root table", file=sys.stderr)
        return 2

    entries = struct.unpack("<512Q", raw)
    ok = True
    for index, value in enumerate(entries):
        if index in EXPECTED:
            if value != EXPECTED[index]:
                print("entry[%d] = 0x%016x, expected 0x%016x"
                      % (index, value, EXPECTED[index]))
                ok = False
        elif value != 0:
            print("entry[%d] = 0x%016x, expected 0" % (index, value))
            ok = False

    print("root table: 512 entries, 4096 bytes, image offset 0x%x" % offset)
    print("entry[2]   = 0x%016x" % entries[2])
    print("entry[511] = 0x%016x" % entries[511])
    print("boot page table check: %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
