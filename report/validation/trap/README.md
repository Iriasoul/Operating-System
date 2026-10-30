# Lab3 Trap 专项验证

本目录只新增 Trap 专项验证材料，不修改 `report/validation/trapentry/`、
`report/validation/boot/` 的历史日志或实验报告正文。正式内核只改了
`code/kern/trap/trap.c` 中未知中断的一行输出。

## 1. 结论

- 未知中断原因改为 `%lu` + `unsigned long`，原因 `0x10000000f` 输出
  `4294967311`，不再被 `int` 截断为 `15`。
- 直接调用处理函数：21 个用例全部通过，覆盖未知中断、11 个除 Supervisor
  时钟外的已知中断、16/32 位非法指令与断点、长编码兜底、其他异常。
- 时钟边界：连续 1000 次 Supervisor 时钟中断逐次通过；第 1000 次完成第十次
  统计后才调用关机替身一次。
- 真实 Trap 返回：正式 `trapentry.S` + 正式 `trap.c` 下，四种显式编码均能
  返回并继续执行；两条 32 位指令位于地址模 4 等于 2 的位置。
- 回归：正式源码正常启动十行 `100 ticks` 后关机；官方 `make grade` 与
  源码一致副本直调官方注入/评分均为 100/100。
- 未发现上述行为之外的新失败；`print_trapframe()` 的 `%08x` 帧字段渲染限制
  见第 7 节，本轮未改，交回复核。

## 2. 未知中断编号修复

修复前：

```c
cprintf("Unknown interrupt: cause %d\n", (int)cause);
```

`cause` 是 `uintptr_t`（RV64 下 64 位）。`0x10000000f` 转成 `int` 后只剩低
32 位的 `15`，输出为 `Unknown interrupt: cause 15`。

修复后：

```c
cprintf("Unknown interrupt: cause %lu\n", (unsigned long)cause);
```

`code/libs/printfmt.c` 的 `%u` 支持 `lflag`，`lflag == 1` 时用
`va_arg(ap, unsigned long)` 取 64 位值，因此完整输出 `4294967311`。原因提取、
分类和 `tf` 的字段都没有改动。

修复前失败证据保存在 `logs/prefix/`：

- `logs/prefix/direct.log`：`unknown-cause-0x10000000f` 与带中断标志的同类
  用例均 FAIL，捕获输出为 `Unknown interrupt: cause 15`；
- `logs/prefix/direct.meta`：记录修复前源码 SHA-256
  `1c16ed56...`、失败结果与 QEMU 返回状态；
- `logs/prefix/fix.diff`：本次一行修改。

修复后 `logs/direct.log` 中同两处输出为
`Unknown interrupt: cause 4294967311`，21 个用例全部 `ok`。

## 3. 测试分层与入口

### 3.1 直接调用处理函数

```sh
sh run_unit.sh
```

- 被测实现：正式 `code/kern/trap/trap.c`，链接正式
  `code/libs/printfmt.c` 和 `code/libs/string.c`。
- 测试替身：`common/console.c` 用有界捕获缓冲 + SBI putchar 代替正式控制台；
  `clock_set_next_event()` 与 `sbi_shutdown()` 只记录调用。
- 不经过 `trapentry.S`/`sret`，直接调用 `interrupt_handler()` 和
  `exception_handler()`。
- 捕获缓冲 8192 字节，`cprintf` 每次格式化用 4096 字节临时缓冲；任何截断都会
  置 `capture_overflow`，并作为断言失败。失败时用 `raw_puts` 直接输出完整
  捕获内容，不再经过短格式化缓冲。

21 个用例：

| 类别 | 用例 | 断言 |
|---|---|---|
| 未知中断 | 15；`0x10000000f`；后者带 bit63 | 完整十进制文本；整帧不变；ticks、时钟、关机替身均不变 |
| 其他已知中断 | U/S/H/M 软件中断、U/H/M 时钟、U/S/H/M 外部中断 | 分类文本；整帧不变；不增加 ticks、不重设时钟、不关机 |
| 非法指令/断点 | 16 位与 32 位各一组；32 位组 epc 位于 2 mod 4 | epc 精确 +2/+4；除 epc 外整帧不变；输出起始文本正确 |
| 长编码 | 低五位 `0x1f`，非法指令与断点各一组 | 输出诊断和 Trap frame；epc 与其他字段不变 |
| 其他异常 | cause=20，epc 使用不可读哨兵 | 只输出 Trap frame；整帧不变；不读取 epc |

整帧比较覆盖 `gpr[32]`、`status`、`epc`、`badvaddr`、`cause`，共
36 个 `uintptr_t` 字（`sizeof(struct trapframe) == 36 * 8`）。

能发现：`int` 截断、丢失 bit63、原因误分类、其他中断误触发时钟/关机、
固定 +4 或固定 +2、在 2 mod 4 地址上错误读取、长编码仍推进 epc、默认异常
分支误读 epc、处理函数改写 Trap frame。

不能发现：真实 `SAVE_ALL`/`RESTORE_ALL`/`sret` 的行为、真实时钟与 SBI
关机、用户态访问、长编码的真实执行。

### 3.2 时钟精确边界

同一 `run_unit.sh` 中构建独立的 `test_clock` 实例。`start.S` 清零 `.bss`，
因此 `ticks` 和 `trap.c` 内部的报告计数都从 0 开始；测试不读取也不重置私有
计数。

连续调用 `interrupt_handler()` 1000 次，逐次断言：

- `clock_set_next_event()` 恰好调用一次；
- `ticks` 恰好加一，最终 `ticks == 1000`；
- 输入 Trap frame 不变；
- 只在第 100、200……1000 次各输出一次 `100 ticks`，共 10 次；
- 第 1 至第 999 次关机替身调用次数为 0；
- 第 1000 次关机替身调用一次，且其记录的“关机前输出事件数”等于当次的
  输出事件数，即统计输出先于关机。

日志 `logs/clock.log` 最后的边界行：

```
[clock] tick=1000 stats=10 shutdown=1 order=1/1
[clock] checked 1000 interrupts: ...
CLOCK BOUNDARY PASS
```

能发现：漏重设时钟、ticks 多次/少次增加、统计提前或延后、提前关机、
关机早于输出、处理函数改写输入帧。

不能证明真实定时器频率、真实 SBI 关机或硬件中断延迟。

### 3.3 真实 Trap 入口与 sret

```sh
sh run_real.sh
```

- 使用正式 `code/kern/trap/trapentry.S` 和正式 `code/kern/trap/trap.c`。
- 仅测试 fixture 使用自己的 `common/start.S`、`common/link.ld`，在
  `0x80200000` 裸机运行；不使用正式 `init.c`，不开启分页，不改正式
  `trapentry.S`、`trap.c` 或 Makefile。本层没有对正式启动文件做注入，也没有
  需要保留的临时副本 diff；正式源码只被编译，未被改写。
- `test_main()` 先调用正式 `idt_init()` 安装 `__alltraps` 为 `stvec`。

四个显式编码与实测地址（来自 `logs/real_disasm.txt`）：

| 用例 | 编码 | 地址 | 地址 mod 4 | 下一条 | 实际推进 |
|---|---|---|---|---|---|
| 16 位断点 | `0x9002` | `0x802004ce` | 2 | `0x802004d0` | 2 |
| 32 位断点 | `0x00100073` | `0x80200562` | 2 | `0x80200566` | 4 |
| 16 位非法指令 | `0x0000` | `0x802005f2` | 2 | `0x802005f4` | 2 |
| 32 位非法指令 | `0x0000007b` | `0x80200686` | 2 | `0x8020068a` | 4 |

每个汇编用例在陷入前设置 `s1`、`s2`、`s3`、`t0`、`t1`、`t2` 哨兵，保存
`ra` 和 `sp`，返回后逐一比较；函数自身保存/恢复 `ra`、`s1`–`s4`，只用
caller-saved 寄存器做检查，返回值放在 `a0`。日志中正式处理函数打印的
`Breakpoint at`/`Illegal instruction at` 地址与反汇编表一致；
`logs/real_encoding_check.txt` 自动核对编码、模 4、下一条指令和日志地址。

能发现：epc 推进错误、`SAVE_ALL`/`RESTORE_ALL` 对这些寄存器和栈的破坏、
`sret` 返回位置错误、测试函数 ABI 破坏（通过保存/恢复和哨兵区分）。

不能替代：队友的全 GPR 上下文测试、分页开启后的返回、用户态 Trap、嵌套
中断、未支持长编码的真实执行。

### 3.4 回归

```sh
sh run_regression.sh
```

- 正常启动：从正式源码 `make clean && make`，用
  `-kernel code/bin/ucore.img` 有界运行；日志
  `logs/regression/normal.log` 显示 DTB、`check_alloc_page() succeeded!`、
  十行 `100 ticks`，QEMU 退出 0，无 panic/FAIL/trapframe。
- 官方评分：沿用 `report/validation/boot/qemu-adapter`，PATH 前置该适配器后
  运行官方 `make grade`；另在源码一致临时副本直调官方 `inject.py` 与
  `grade.sh` 保留子项日志。两个入口均 `Total Score: 100/100`，退出 0。
- 新日志只写入 `logs/regression/`，未覆盖 `report/validation/boot/logs/`。

## 4. 运行入口

```sh
cd report/validation/trap

sh run_all.sh        # 单元 + 真实返回 + 回归，全部通过才输出 ALL TRAP VALIDATION PASS
sh run_unit.sh       # 直接处理函数 + 时钟边界
sh run_real.sh       # 真实 trapentry.S + trap.c 返回
sh run_regression.sh # 正常启动 + 官方评分回归
```

可覆盖环境变量：

- `RISCV_BIN`：交叉工具链目录，默认 `/opt/riscv/bin`
- `QEMU` 或 `QEMU_REAL`：真实 `qemu-system-riscv64`，默认
  `/opt/qemu/bin/qemu-system-riscv64`

临时构建目录用 `mktemp -d` 创建并在退出时删除；正式 `code/` 不写入测试
源码。QEMU 失败时返回码不一定非零（`test_exit` 的 `0x3333` 在本机 QEMU 7.0
仍返回 0），因此所有脚本都以完整 PASS 标记和失败标记为准，超时或缺少标记
均判失败。

## 5. 结果汇总

| 层 | 入口 | 结果 | 关键日志 |
|---|---|---|---|
| 直接处理函数 | `run_unit.sh` | 21/21 ok，`DIRECT TESTS PASS` | `logs/direct.log` |
| 时钟边界 | `run_unit.sh` | 1000 次逐次通过，`CLOCK BOUNDARY PASS` | `logs/clock.log` |
| 真实返回 | `run_real.sh` | 4/4 ok，编码核对 PASS | `logs/real.log`、`logs/real_disasm.txt`、`logs/real_encoding_check.txt` |
| 正常启动回归 | `run_regression.sh` | 十行 ticks，QEMU exit 0 | `logs/regression/build.log`、`normal.log` |
| 官方评分回归 | `run_regression.sh` | 100/100（make grade 与直调均通过） | `logs/regression/make_grade.out`、`grade_direct.out`、`.qemu.*.out` |

`run_all.sh` 最终输出：

```
ALL TRAP VALIDATION PASS
```

## 6. 源码指纹与历史证据

- HEAD：`3a4b0f2`（本轮未提交）。
- 修复前 `trap.c` SHA-256：`1c16ed56...`（来自 HEAD）。
- 修复后 `trap.c` SHA-256：`976a91fb...`。
- `entry.S` SHA-256：`afd6309b...`（本轮未改）。
- `trapentry.S` SHA-256：`e6b25bb1...`（本轮未改）。

这些值记录在 `logs/fingerprints.txt`、`logs/unit.meta`、`logs/real.meta`、
`logs/regression/regression.meta` 和 `logs/prefix/direct.meta`。上一轮
`report/validation/boot/` 的日志保持原样，本轮回归结果写入新目录。

## 7. 仍未覆盖与已知问题

- **`print_trapframe()` 的帧字段渲染限制**：该函数用 `%08x` 打印 64 位的
  `status`/`epc`/`badvaddr`/`cause`。大编号用例中，`tf->cause` 字段经整帧
  比较确认保持为 `0x10000000f`，但帧转储显示
  `cause    0x0000000f`（见 `logs/direct.log` 第 83 行）。这是独立于
  “Unknown interrupt” 行的既有输出截断；本轮允许的正式修改只有未知中断
  输出语句，因此未改此函数，交回复核。
- 直接调用测试不执行真实 `trapentry.S`/`sret`；真实返回测试不开启分页、
  不使用正式 `init.c`，也不覆盖用户态、嵌套中断或 48/64 位长编码的真实
  执行。
- 时钟边界测试使用记录型时钟/关机替身，不代表真实定时器频率或 SBI 关机
  时序；真实关机集成由回归的正常启动覆盖。
- 真实返回测试检查 `s1`–`s3`、`t0`–`t2`、`ra`、`sp` 和栈；全 GPR 上下文
  仍由队友的 `report/validation/trapentry/` 测试覆盖，本轮未重写。
- `make grade` 会清理 `code/bin` 并在临时副本构建；回归后如需可执行镜像，
  重新运行 `make` 或 `report/validation/boot/run_normal.sh`。
