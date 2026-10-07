"""Run local lab1 checks against the supplied course framework."""
import argparse
import json
import socket
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LOGS = ROOT / "validation"
LOGS.mkdir(exist_ok=True)
parser = argparse.ArgumentParser()
parser.add_argument("--mode", choices=["all", "boot", "gdb"], default="all")
parser.add_argument("--qemu", default="qemu-system-riscv64")
parser.add_argument("--gdb", default="riscv64-unknown-elf-gdb")
args = parser.parse_args()
QEMU_ARGS = [args.qemu, "-machine", "virt", "-smp", "1", "-m", "128M",
             "-nographic", "-bios", "default", "-kernel", "bin/ucore.img"]


def stop(process):
    if process.poll() is None:
        process.terminate()
    try:
        return process.communicate(timeout=2)[0]
    except subprocess.TimeoutExpired:
        process.kill()
        return process.communicate()[0]


def check_boot():
    process = subprocess.Popen(QEMU_ARGS, cwd=ROOT, stdin=subprocess.DEVNULL,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    timed_out = False
    try:
        output, _ = process.communicate(timeout=3)
    except subprocess.TimeoutExpired:
        timed_out = True
        output = stop(process)
    finally:
        if process.poll() is None:
            stop(process)
    text = output.decode("utf-8", errors="replace")
    (LOGS / "boot.log").write_text(text, encoding="utf-8")
    if not timed_out or "(THU.CST) os is loading ..." not in text:
        raise RuntimeError("kernel boot output or wait loop missing; see validation/boot.log")
    print("PASS: supplied kernel prints the expected boot message and stays running")
    return {"boot": "PASS"}


def check_gdb():
    with socket.socket() as server:
        server.bind(("127.0.0.1", 0))
        port = server.getsockname()[1]
    commands = rf"""
set pagination off
set confirm off
set architecture riscv:rv64
set tcp auto-retry on
set tcp connect-timeout 5
file bin/kernel
target remote 127.0.0.1:{port}
if $pc != 0x1000
    echo FAIL: reset PC\n
    quit 1
end
printf "Reset PC = 0x%lx\n", $pc
x/6i $pc
set $steps = 0
while $pc != 0x80000000 && $steps < 16
    stepi
    set $steps = $steps + 1
end
if $pc != 0x80000000
    echo FAIL: MROM did not enter OpenSBI\n
    quit 1
end
printf "OpenSBI PC = 0x%lx; reset instructions executed = %d\n", $pc, $steps
hbreak *kern_entry
continue
if $pc != 0x80200000
    echo FAIL: kernel entry\n
    quit 1
end
printf "Kernel entry PC = 0x%lx\n", $pc
x/4i $pc
hbreak *kern_init
continue
printf "C entry PC = 0x%lx; SP = 0x%lx; bootstacktop = 0x%lx\n", $pc, $sp, &bootstacktop
if $sp != (unsigned long)&bootstacktop
    echo FAIL: stack initialization\n
    quit 1
end
if ((unsigned long)$sp & 15) != 0
    echo FAIL: stack alignment\n
    quit 1
end
if (unsigned long)&bootstacktop - (unsigned long)&bootstack != 8192
    echo FAIL: stack size\n
    quit 1
end
hbreak *memset
continue
printf "memset: dest=0x%lx value=%ld length=%ld\n", $a0, $a1, $a2
if $a0 != (unsigned long)&edata || $a1 != 0 || $a2 != (unsigned long)&end - (unsigned long)&edata
    echo FAIL: initialization range\n
    quit 1
end
if (unsigned long)&end > (unsigned long)&edata
    set {{unsigned char}}&edata = 0xa5
end
hbreak *cprintf
continue
printf "BSS range: edata=0x%lx end=0x%lx\n", &edata, &end
set $cursor = (unsigned long)&edata
while $cursor < (unsigned long)&end
    if *(unsigned char *)$cursor != 0
        echo FAIL: nonzero BSS byte\n
        quit 1
    end
    set $cursor = $cursor + 1
end
if (unsigned long)&end == (unsigned long)&edata
    echo BSS is empty in this supplied lab1 build; memset arguments verified.\n
else
    echo BSS contents verified after memset.\n
end
printf "cprintf format and message:\n"
x/s $a0
x/s $a1
echo PASS: MROM -> OpenSBI -> kernel; stack and initialization verified\n
disconnect
quit 0
"""
    (LOGS / "check.gdb").write_text(commands, encoding="utf-8")
    with (LOGS / "debug-qemu.log").open("w", encoding="utf-8") as log:
        process = subprocess.Popen(QEMU_ARGS + ["-S", "-gdb", f"tcp:127.0.0.1:{port}"],
                                   cwd=ROOT, stdin=subprocess.DEVNULL,
                                   stdout=log, stderr=subprocess.STDOUT)
        try:
            result = subprocess.run([args.gdb, "-q", "-nx", "-batch", "-x", "validation/check.gdb"],
                                    cwd=ROOT, capture_output=True, text=True, timeout=30)
            text = result.stdout + result.stderr
            (LOGS / "gdb.log").write_text(text, encoding="utf-8")
            print(text, end="")
            if result.returncode or "PASS: MROM" not in text:
                raise RuntimeError("GDB check failed; see validation/gdb.log")
        finally:
            stop(process)
    return {"startup_chain": "PASS", "stack": "PASS", "initialization": "PASS"}


try:
    results = {}
    if args.mode in ("all", "boot"):
        results.update(check_boot())
    if args.mode in ("all", "gdb"):
        results.update(check_gdb())
    (LOGS / "results.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    print(f"Lab1 local checks: {len(results)}/{len(results)} PASS (not an official grading score)")
except (OSError, subprocess.TimeoutExpired, RuntimeError) as error:
    sys.exit(f"FAIL: {error}")
