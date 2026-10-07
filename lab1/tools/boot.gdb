# In another terminal, run make debug with the default port.
set pagination off
set architecture riscv:rv64
file bin/kernel
target remote 127.0.0.1:1234
info registers pc
x/6i $pc
# Six instructions in the tested QEMU 7.0.0 MROM reach OpenSBI.
stepi 6
info registers pc a0 a1 a2
hbreak *kern_entry
continue
x/4i $pc
info registers pc sp
hbreak *kern_init
continue
info registers pc sp
p/x &bootstacktop
