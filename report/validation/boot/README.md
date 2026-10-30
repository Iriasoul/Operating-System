# Lab3 启动映射修复与本地验证

本目录只保存本轮启动映射修复的验证材料，不改写 `report/validation/trapentry/`，
也不修改实验报告正文。

## 1. 结论

- `code/kern/init/entry.S` 在根页表索引 2 增加低地址 DRAM 恒等映射；根表仍为
  512 项、4096 字节，索引 511 的原高地址映射不变。
- 正式源码正常启动：QEMU 退出状态 0，十行 `100 ticks`，约 10 秒后由内核
  `sbi_shutdown()` 主动关机，无 panic、FAIL 或重复 trapframe。
- 正式源码经官方 `make grade` 评分：三个子项通过，总分 100/100，退出状态 0。
- 当前 QEMU 7.0 需要参数适配器处理 `-device loader`；适配器、官方注入和正式
  源码结果在本文中分开说明。

## 2. 为什么开启分页后仍需要低地址映射

启动时 PC 位于物理地址 `0x80200000` 附近。`bin/kernel` 反汇编中的顺序是：

```
ffffffffc0200034  csrw satp,t0
ffffffffc0200038  sfence.vma
...
ffffffffc020004a  lui  t0,...
ffffffffc020004e  addi t0,t0,84   # kern_init
```

`csrw satp` 后，下一次取指在物理地址 `0x80200038`。Sv39 根级索引取虚拟地址的
bits[38:30]：

```
0x0000000080200038 >> 30 = 2
```

修复前根表只有索引 511 有效，索引 2 为空，因此开分页后的第一次取指就触发
instruction page fault。上一轮的 QEMU `-d int` 记录为 `cause=0xc`、
`epc=tval=0x80200038`：`sfence.vma`、栈设置和高地址跳转都还没有执行。

修复后两个有效叶项：

| 根索引 | 虚拟地址范围 | 物理地址范围 | PTE |
|---|---|---|---|
| 2 | `[0x80000000, 0xc0000000)` | `[0x80000000, 0xc0000000)` | `0x200000cf` |
| 511 | `[0xffffffffc0000000, 2^64)` | `[0x80000000, 0xc0000000)` | `0x200000cf` |

- 索引 2 覆盖低地址 1 GiB，使 `csrw satp` 之后的低地址 PC 能继续取指，完成
  `sfence.vma`、设置高地址栈并 `jr` 到 `kern_init`。
- 索引 511 覆盖高地址 1 GiB，使内核进入 `0xffffffffc0200054` 后继续按原有
  偏移访问代码、栈和数据。
- `0x200000cf` 表示 PPN=0x80000（PA `0x80000000`），V/R/W/X/A/D 均为 1，
  U=0（仅 S 模式），G=0；两个叶项权限相同，沿用原教学映射。
- 高窗口的排他上界是数学意义上的 2^64，程序中没有用普通 `uint64_t` 表示它；
  页表项只描述这 1 GiB 窗口。
- 根表 512×8 = 4096 字节。符号地址显示 `boot_page_table_sv39` 在
  `0xffffffffc0206000`，`boot_hartid` 在 `0xffffffffc0207000`，恰好相隔
  0x1000，说明没有因新增叶项移动后续布局。

## 3. 修改范围

正式内核只改 `code/kern/init/entry.S`（+9/−4）：

- 把 `.zero 8 * 511` 拆成索引 2 的叶项、508 个空项和索引 511 的叶项；
- 同步修正页表附近已经失效的注释。

保存 hartid/DTB、计算 satp、`sfence.vma`、设置精确栈顶、跳转 `kern_init`
均保持原样。`code/kern/trap/trap.c` 是上一轮未提交的实现，本轮原样保留。
未修改 `trapentry.S`、内存管理、时钟、SBI、链接脚本、Makefile 或 `tools/`。

工作区状态（本轮结束时）：

```
 M code/kern/init/entry.S
 M code/kern/trap/trap.c
?? report/validation/boot/
```

HEAD 为 `8108993`，未提交。`entry.S` 的 SHA-256 为
`afd6309b38888d9500ee3d723f7d0db7c69f24e5a5280430ba4bc0ccd10d15f6`；
`trap.c` 为
`1c16ed56217ec4168d9d0e3a515a9e9782e15cc3b8f2b6fa411743c1a8e6dd7a`。
这些值与 `logs/normal.meta`、`logs/grade/grade.meta` 中的记录一致。

## 4. 环境与验证入口

实际环境：

- WSL Ubuntu 26.04
- `/opt/riscv/bin/riscv64-unknown-elf-gcc`，SiFive GCC 10.2.0
- `/opt/qemu/bin/qemu-system-riscv64`，QEMU 7.0.0

### 4.1 正常启动

```sh
cd report/validation/boot
sh run_normal.sh
```

脚本执行：

1. `make clean` + `make`，从本轮正式源码重新构建；
2. 从 `bin/ucore.img` 读取根表，校验 512 项、4096 字节、仅索引 2 和 511
   有效且均为 `0x200000cf`；
3. 直接调用 QEMU，加 `-kernel code/bin/ucore.img` 和 30 秒外部上限；
4. 校验 DTB 初始化、`page_init`、`check_alloc_page() succeeded!`、十行
   `100 ticks`，以及无 panic/FAIL/重复 `trapframe at`；
5. 把命令、退出状态、工具版本和源码指纹写入 `logs/normal.meta`。

可覆盖的环境变量：`RISCV_BIN`、`QEMU`（或 `QEMU_REAL`）、`RUN_TIMEOUT`。

### 4.2 官方评分

```sh
cd report/validation/boot
sh run_grade.sh
```

脚本记录两个入口：

1. 在 `code/` 运行官方 `make grade`，PATH 前置本目录的 QEMU 适配器；
2. 从当前正式源码创建源码一致的临时副本，直接调用官方
   `tools/inject.py` 和 `tools/grade.sh`，只做官方测试注入，不修改内核源码，
   以保留 `make grade` 会删除的 `.qemu.*.out` 子项日志。

### 4.3 参数适配器

`qemu-adapter/qemu-system-riscv64` 只做一种转换：

```
-device loader,file=<path>,addr=0x80200000   ->   -kernel <path>
```

- 其他 `-device loader,...` 形式、其他 `addr`、多个 loader、已经含 `-kernel`
  的调用一律报错并退出 2，不静默猜测。
- `-serial`、`-monitor`、`-no-reboot` 等其余参数原样传递。
- 真实模拟器由 `QEMU_REAL` 指定，默认 `/opt/qemu/bin/qemu-system-riscv64`，
  不把个人路径写死为唯一方式。
- 采用它的原因：本机 QEMU 7.0/OpenSBI 不把 `-device loader` 的 `addr` 当作
  下一阶段入口；未适配时 OpenSBI 打印 `Next Address: 0x0`。
- 这是**参数适配下运行官方评分**，不是官方原始运行环境；Makefile 和
  `tools/` 均未修改。换用能正确处理该 loader 参数的 QEMU 时可不使用适配器。
  适配器对不支持形式的拒绝行为记录在 `logs/adapter_reject.log`（三种情况均
  退出 2）。
- 注意：官方 `make grade` 会先在 `code/` 执行 `make clean`，再在临时副本中
  构建；因此评分结束后 `code/bin` 可能为空。需要可执行镜像时重新运行
  `make` 或 `sh run_normal.sh`。

## 5. 实际结果

### 5.1 正式源码正常启动

```sh
sh run_normal.sh
```

结果：`QEMU exit=0 ticks=10`，`normal boot verification: PASS`，脚本退出 0。
串口日志 `logs/normal.log` 显示 OpenSBI 的 `Next Address: 0x80200000`、DTB
内存信息、`check_alloc_page() succeeded!`、`++ setup timer interrupts` 和十行
`100 ticks`。`logs/normal.meta` 记录退出状态 0 与实际命令。

### 5.2 官方评分

```sh
sh run_grade.sh
```

结果：`make_grade_exit=0`、`direct_grade_exit=0`，两个入口都输出
`Total Score: 100/100`，脚本退出 0。

归档的子项日志来自源码一致的临时副本中的官方注入/评分流程：

| 日志 | 关键计数 | 结果 |
|---|---|---|
| `.qemu.GRADE_LAB3_CONTEXT.out` | `Breakpoint at` ×1；FAIL ×0；panic ×0 | PASS 35/35 |
| `.qemu.GRADE_LAB3_ILLEGAL.out` | `Illegal instruction at` ×1、`Supervisor software interrupt` ×1、`Unknown interrupt: cause 15` ×1、`cause    0x00000014` ×1 | PASS 25/25 |
| `.qemu.GRADE_LAB3_TIMER.out` | `100 ticks` ×10、`ticks=200` | PASS 40/40 |

CONTEXT 与 ILLEGAL 日志只有 7 行 ticks，是因为 `grade.sh` 对这两个用例设了
8 秒上限，测试标记在这之前已经输出；TIMER 用例等待到 ticks=200，并在第十次
统计后由内核关机。两种日志都没有 FAIL、panic 或重复异常。

### 5.3 三个层面的区分

- **正式源码结果**：上述正常启动和评分都基于本轮 `entry.S` 修复后的正式源码，
  指纹与工作区一致。
- **参数适配**：QEMU 7.0 的 loader 入口问题由本目录适配器处理；这是运行环境
  适配，不是内核源码补丁。
- **官方测试注入**：`make grade` 在临时副本中调用官方 `inject.py` 和
  `grade.sh`，由官方脚本添加 `kern/test` 并修改副本的 `init.c`；本目录的直调
  入口重复同一官方流程以保留子项日志。正式源码未被测试注入改写。

## 6. 证据清单

```
logs/build.log                      clean 构建输出
logs/page_table_check.log           根表 512 项静态检查
logs/normal.log                     正式源码串口日志
logs/normal.meta                    命令、退出状态、版本、源码/镜像 SHA-256
logs/qemu_stderr.log                QEMU 标准错误（空）
logs/grade/make_grade.out           官方 make grade 输出
logs/grade/grade_direct.out         官方注入/评分的直调输出
logs/grade/.qemu.*.out              三个子项的串口日志
logs/grade/grade.meta               适配规则、两个入口退出状态、源码指纹
logs/adapter_reject.log             适配器对不支持参数形式的拒绝记录
```

复现方法：在本目录依次执行 `sh run_normal.sh` 和 `sh run_grade.sh`。
`git diff --check` 退出 0；输出中仅有 `code/tools/gdbinit` 的既有 CRLF
提示，该文件本轮未修改。

上一轮影子树的日志（`C:\Users\lenovo\AppData\Local\Temp\lab3_trap_verify`）
保留其历史来源说明，未改名，也未作为本轮正式源码的验证结果。

## 7. 剩余问题

- 默认 `make qemu` 仍使用 `-device loader`；在本机 QEMU 7.0 下需要本目录的
  适配器，或换用能正确设置入口的 QEMU。按本轮约束未修改 Makefile。
- Trap 指令长度边界（非压缩 32 位 `ebreak`、更长编码）、更完整的状态保持和
  未知中断大编号输出，留待下一轮专项验证。
- 实验报告正文尚未更新。
