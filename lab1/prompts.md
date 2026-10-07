# Lab1 最终提示词与行为规格

根据提供的框架和最终实现整理，不是原始对话逐字转录。实际过程见 [report.md](report.md)。

## 提示词 1

~~~~markdown
[PROMPT]
任务：阅读提供的 lab1/kern/init/entry.S、kern/init/init.c、kern/mm/mmu.h、kern/mm/memlayout.h 和 tools/kernel.ld，在 lab1/report.md 完成练习一。
操作要求：必须修改实际报告文件，保留这些已有的启动代码，不另写一套内核；结合实际反汇编解释源码。
输出要求：回答 la sp, bootstacktop 与 tail kern_init 的操作和目的，说明栈、调用约定及伪指令展开。

[RELY]
#define PGSIZE 4096
#define PGSHIFT 12
#define KSTACKPAGE 2
#define KSTACKSIZE (KSTACKPAGE * PGSIZE)
入口符号 kern_entry，链接地址 0x80200000，栈符号 bootstack、bootstacktop。
int kern_init(void) __attribute__((noreturn));
void *memset(void *s, char c, size_t n);
int cprintf(const char *fmt, ...);
edata、end 来自提供的链接脚本。

[GUARANTEE]
报告回答两条指令的作用和目的，并说明 kern_entry、kern_init、memset 和 cprintf 的关系。
保持已有函数签名，不把符号地址与符号处的数据混为一谈。

[SPECIFICATION]
kern_entry：
Pre-Condition：固件已移交控制权，内核还未建立自己的 C 栈。
Post-Condition：解释 C 入口之前 sp 等于 bootstacktop，16 字节对齐，栈向低地址增长。
Requirements：说明为何必须先建立栈，再执行 C 代码。

tail：
Pre-Condition：栈已建立，kern_init 不返回。
Post-Condition：解释控制权转移及不建立返回入口的新返回地址；给出本次实际展开。
Requirements：区分源码伪指令与随工具链、链接配置变化的机器指令。

kern_init：
Pre-Condition：栈与链接符号有效。
Post-Condition：解释清零 [edata,end)、输出信息与无限循环。
Requirements：不能清零正在使用的栈；空 BSS 不得描述成非空探针清零测试。
~~~~

## 提示词 2

~~~~markdown
[PROMPT]
任务：修正 lab1/libs/sbi.c 的 sbi_call 内联汇编寄存器约束，补齐现有 cons_getc 调用而未定义的 sbi_console_getchar。
操作要求：必须修改实际文件，保留编号、函数签名及其他框架代码，不重写 console、stdio、printfmt 和 string 库。
输出要求：使用课程依赖的 legacy SBI，解释原约束的潜在风险，不将静态问题写成已经发生的运行故障。

[RELY]
libs/defs.h：typedef unsigned long long uint64_t;
SBI_SET_TIMER = 0，SBI_CONSOLE_PUTCHAR = 1，SBI_CONSOLE_GETCHAR = 2。
void sbi_console_putchar(unsigned char ch);
void sbi_set_timer(unsigned long long stime_value);
int sbi_console_getchar(void);
cons_getc 已使用字符输入接口，cons_putc 已使用字符输出接口。

[GUARANTEE]
uint64_t sbi_call(uint64_t sbi_type, uint64_t arg0, uint64_t arg1, uint64_t arg2);
int sbi_console_getchar(void);
保持既有 sbi_console_putchar、sbi_set_timer 和头文件接口。

[SPECIFICATION]
sbi_call：
Pre-Condition：S-mode 能调用 legacy SBI，固件支持对应扩展。
Post-Condition：ecall 前 a7 为编号，a0/a1/a2 为实参，返回 a0 的结果。
Requirements：用固定寄存器操作数描述输入与返回关系，保留 memory clobber，避免使用约束不充分的 mv 序列。

sbi_console_getchar：
Pre-Condition：固件支持字符输入扩展。
Post-Condition：调用编号 2，将固件结果转换为 int 返回。
Case 1：有字符时返回字符。
Case 2：无输入时保留固件返回语义，不伪造字符。
Requirements：本次启动检查没有交互式输入测试，报告不能声称输入路径已实测。
~~~~

## 提示词 3

~~~~markdown
[PROMPT]
任务：在提供的 lab1 框架中完成练习二的 QEMU/GDB 跟踪，补齐 Makefile 引用但缺失的 tools/grade.sh，保存真实日志并生成报告验证图。
操作要求：必须修改实际文件，保留原入口、初始化和基础库；只调整必要的构建或启动参数，不替换内核，不伪造成功结果。
输出要求：提供 make qemu、debug、gdb、grade、check、check-gdb 的复现步骤；把实际观察写入 report.md。

[RELY]
原 Makefile 使用 riscv64-unknown-elf 工具链与 QEMU virt，grade 引用了缺失的 grade.sh。
kern_entry、kern_init、bootstack、bootstacktop、edata、end 来自原框架。
原 init.c 输出 (THU.CST) os is loading ... 后无限循环。
现有环境：WSL CompilerLab；GCC 15.1.0、QEMU 7.0.0、OpenSBI 1.0、GDB 16.3.90.20250610-git。

[GUARANTEE]
保留 Makefile 的构建结构；提供 tools/grade.sh、tools/verify_lab1.py、tools/boot.gdb。
验证必须运行本次提供的框架；本地 grade 不冒充官方评分。

[SPECIFICATION]
构建与启动：
Pre-Condition：工具链可用，原链接脚本可以生成镜像。
Post-Condition：OpenSBI 下一阶段地址为 0x80200000，内核输出启动信息并持续运行。
Case 1：原 -device loader 在当前固件下出现入口 0x0，记录失败后使用 -kernel 加载同一个框架镜像。
Case 2：工具不可用、消息缺失、QEMU 提前退出，返回失败并保留证据。
Requirements：区分主动终止与提前退出，回收自己启动的进程。

GDB 跟踪：
Pre-Condition：QEMU 以 -S 暂停，GDB 使用匹配的 ELF 符号。
Post-Condition：记录 0x1000 的初始指令并单步到 OpenSBI 0x80000000；在原 kern_entry 0x80200000 命中断点，进入 C 前 sp 等于 bootstacktop，栈大小 8192 字节且 16 字节对齐。
Requirements：停在函数的第一条机器指令，避免把执行序言后的 sp 当作初始栈顶。

初始化检查：
Pre-Condition：执行到 memset 的入口，edata/end 为实际链接符号。
Post-Condition：按 ABI 寄存器检查目标、填充值和长度；非空时可写入非零字节并检查清零，空区间明确记录为空。
Requirements：结合寄存器和反汇编判断优化后的变量显示；不向内核新增人工探针替代原构建状态。

报告证据：
Pre-Condition：已经执行检查并捕获真实输出。
Post-Condition：保留完整编译、QEMU、GDB、grade 日志，渲染为图片并嵌入模板。
Requirements：说明日志输出图与桌面截图的区别；不得把 4/4 本地检查写成官方满分。
~~~~
