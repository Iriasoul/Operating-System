# Lab1 提示词汇总与行为规格

根据提供的框架和最终实现整理，不是原始对话逐字转录。实际过程见 [report.md](report.md)。

## 提示词 1

~~~~markdown
[PROMPT]
任务：阅读提供的 code/kern/init/entry.S、kern/init/init.c、kern/mm/mmu.h、kern/mm/memlayout.h 和 tools/kernel.ld，在 report/report.md 完成练习一。
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
任务：修正 code/libs/sbi.c 的 sbi_call 内联汇编寄存器约束，补齐现有 cons_getc 调用而未定义的 sbi_console_getchar。
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
Post-Condition：保留完整编译、QEMU、GDB、grade 日志，最终报告嵌入用户提供的真实终端截图。
Requirements：截图原样保存，不把日志渲染图当作终端截图；不得把 4/4 本地检查写成官方满分。
~~~~

## 提示词 4：按课程提交规范整理分支

~~~~markdown
[PROMPT]
任务：重新整理 Git 仓库的 lab1 分支，一个分支对应一次实验。
操作要求：仅保留 code 和 report 两个交付文件夹；用补全后的课程框架替换 code 内的初始代码；把报告、提示词、验证图片移入 report。
输出要求：report/report.md、report/prompt.md、report/images/ 存在，报告格式遵循用户模板，修正相对链接，验证后上传 GitHub 的 lab1 分支。

[RELY]
已完成的框架、实验报告、提示词及真实验证日志；当前 Git 分支 lab1。
三位成员为石晨昊、王策、吕昊远，均使用 Codex；学号和具体模型版本未提供。

[GUARANTEE]
code/ 包含实现后的源代码；report/ 包含报告、提示词、图片及原始日志。
保留课程原始模板和已有分支历史，不编造个人信息、测试结果或官方评分。

[SPECIFICATION]
目录整理：
Pre-Condition：工作树干净，已确认完成版与初始版的位置。
Post-Condition：根目录交付文件夹仅为 code/ 和 report/，不存在重复的 lab1/ 或 docs/。
Requirements：复制内容后再删除冗余目录，删除目标必须在本仓库内。

迁移验证：
Pre-Condition：脚本和文档路径均已调整。
Post-Condition：从 code/ 构建成功，QEMU 输出启动消息，本地四项检查及实际 make debug 调试通过；报告图片和日志链接有效。
Requirements：将真实输出写到 report/validation/，区分日志渲染图与桌面截图。

提交：
Pre-Condition：最终结构、差异和检查结果已核对。
Post-Condition：变更提交并推送 origin/lab1，核对远程提交与本地一致。
Requirements：本次目录整理只提交 lab1 分支。
~~~~

## 用户任务与修订要求记录

以下记录本次实验会话中可见的用户要求；上面的四段式提示词按最终实现整理，并非原始对话的逐字转录。此前未保留的内部工具调用不冒充完整原始记录。

1. 阅读课程手册并补全代码，为三人小组写分工。
2. 先完成 lab1（最小可执行内核）。
3. 组员：石晨昊，王策，吕昊远。
4. 核实实验一要求、代码、提示词及实验报告是否完成。
5. 用户提供 lab1 代码后，删除此前独立编写的内核，补全用户给的代码。
6. 用户提供实验报告模板，要求按照模板编写。
7. 三人均使用 Codex。
8. 上传到 GitHub，并将交付物放入 lab1 分支。
9. 按最新提交规范重新整理：一个分支对应一次实验，分支命名 labx；code/ 放实现后的代码；report/ 放 report.md、汇总所有提示词的 prompt.md 和报告引用的测试图片 images/。

## 提示词 5：替换为真实终端截图

~~~~markdown
[PROMPT]
任务：将实验报告中的日志渲染图替换为用户提供的真实终端截图。
操作要求：检查四张截图是否分别涵盖编译、启动、GDB 启动链和最终检查结果；原样复制，不修改截图内容；更新报告引用及图片来源说明，并推送 lab1 分支。
输出要求：report/images/ 包含 build.png、qemu.png、gdb_startup.png 和 test_result.png，报告逐张说明所展示的结果。

[RELY]
用户已运行 make -B -j2、make qemu、make grade，并提供四张终端截图。
启动消息为 (THU.CST) os is loading ...；本地检查最终显示 4/4 PASS。
此前保存的文本日志来自自动验证，与本次用户截图属于分别执行的记录。

[GUARANTEE]
保留截图中的真实警告与测试输出，报告不再将提交图片描述为日志渲染图。
保留本地验证与官方评分的区别；不将空 BSS 描述成非空 BSS 清零测试。

[SPECIFICATION]
截图核对：
Pre-Condition：四张源截图可读取，终端输出清晰。
Post-Condition：确认编译完成、OpenSBI 移交内核、启动链地址、栈顶及最终 PASS，文件原样复制。
Requirements：保留来源文件名和 SHA-256，不裁剪或重绘。

文档与提交：
Pre-Condition：截图复制完成。
Post-Condition：报告嵌入全部四张截图，相关说明一致，GitHub lab1 分支包含更新。
Requirements：避免辅助日志渲染工具覆盖真实截图。
~~~~

用户补充要求：“把这些图换成终端截图，你给我命令，我来截图”。随后用户提供四张截图并要求核对。
