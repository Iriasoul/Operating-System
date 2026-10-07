
set pagination off
set confirm off
set architecture riscv:rv64
set tcp auto-retry on
set tcp connect-timeout 5
file bin/kernel
target remote 127.0.0.1:38890
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
    set {unsigned char}&edata = 0xa5
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
