# Lab3 提示词——石晨昊负责部分

本文件采用课程模板的 [PROMPT]、[RELY]、[GUARANTEE]、[SPECIFICATION] 四部分结构，以下是每轮使用的提示词：

## 提示词 1：Trap 上下文恢复与 Trap 入口

~~~~markdown
[PROMPT]

任务：
完成 ucore Lab3 的功能模块一：Trap 上下文恢复与 Trap 入口。
在 code/kern/trap/trapentry.S 的两处“#lab3 function1 YOUR CODE”位置，
分别实现 RESTORE_ALL 和 __alltraps 中调用 C 语言 trap() 的部分，
建立“保存上下文 → 调用 C 处理函数 → 恢复上下文 → sret 返回”的流程。

负责人：2412449-石晨昊。

操作要求：
- 直接修改实际文件，只填写上述两处待实现位置。
- 保留 lab3 function1 标记，将 YOUR CODE 替换为学号 2412449。
- 不修改 SAVE_ALL、struct pushregs、struct trapframe、trap() 的签名及已有 __trapret/sret。
- 不实现 interrupt_handler()、exception_handler()，不修改其他成员负责的代码。
- 不改变 Trap frame 布局，不增加内存分配或全局状态。
- 在 lab3 分支工作，保留当前工作区改动；不执行 commit 或 push。
- 实现阶段不修改实验报告。

输出要求：
解释寄存器槽位与 Trap frame 的对应关系、sp 为什么最后恢复，
以及当前 Trap frame 如何作为参数传给 trap()。
列出实际验证结果和未覆盖范围；不能把独立模块测试写成完整实验评分通过。


[RELY]

开始前阅读并核对：
- code/kern/trap/trapentry.S
- code/kern/trap/trap.h
- code/kern/trap/trap.c
- code/libs/riscv.h
- code/tools/test_lab3_context.S、test_lab3.c、grade.sh

1. 目标平台与访问宏

当前目标为 RV64；REGBYTES=8，LOAD 为 ld，STORE 为 sd。
实现时使用现有宏，不把槽位偏移写成另一套常量。

2. Trap frame 布局

struct trapframe {
    struct pushregs gpr;
    uintptr_t status;
    uintptr_t epc;
    uintptr_t badvaddr;
    uintptr_t cause;
};

gpr 按 x0～x31 的寄存器编号顺序存放，每项为 uintptr_t：
- x0：0*REGBYTES
- x1(ra)：1*REGBYTES
- x2(sp)：2*REGBYTES
- x3～x31：对应编号乘 REGBYTES
- status：32*REGBYTES
- epc：33*REGBYTES
- badvaddr：34*REGBYTES
- cause：35*REGBYTES

RV64 下共 36*8=288 字节，C 结构与汇编保存布局必须一致。

3. 已有 SAVE_ALL

SAVE_ALL 先把 Trap 发生前的 sp 放入 sscratch，
再令 sp 减少 36*REGBYTES，为 Trap frame 分配空间。
它保存 x0、x1、x3～x31，并通过 sscratch 取得原始 sp，写入槽位 2。
它把 sstatus、sepc、sbadaddr、scause 分别写入槽位 32～35。
csrrw s0, sscratch, x0 同时将 sscratch 清零。
因此，宏执行后 sp 指向完整 Trap frame，而槽位 2 是进入 Trap 前的 sp。

4. C 语言处理接口

void trap(struct trapframe *tf);

现有 trap() 通过 trap_dispatch() 分发中断和异常。
RISC-V 整数调用约定使用 a0 传递第一个参数，jal 更新 ra。
原始 a0、ra 已由 SAVE_ALL 保存，可以在调用前使用它们。
trap() 正常返回后，sp 应仍指向当前 Trap frame。

5. 已有返回入口

__trapret:
    RESTORE_ALL
    sret

RESTORE_ALL 负责恢复软件保存的状态，已有 sret 完成特权返回。
本实验当前处理内核态发生的 Trap，不据此扩展用户栈切换或进程切换。


[GUARANTEE]

实现：
- RESTORE_ALL：按现有 Trap frame 恢复 sstatus、sepc 和 x1～x31。
- __alltraps：在 SAVE_ALL 后，将当前 Trap frame 地址作为 trap() 的第一个参数传入，
  调用 trap()；返回后衔接已有 __trapret。

不改动已有保存流程和数据布局；不修改其他模块。
sbadaddr 和 scause 的保存值用于分析 Trap，本模块不要求把它们写回 CSR。


[SPECIFICATION]

## RESTORE_ALL

Pre-Condition：
- 当前处于 S 模式，sp 指向由 SAVE_ALL 建立的有效 Trap frame。
- C 处理函数已经返回，帧中保存了应恢复的寄存器值和返回状态。
- 返回状态合法；本实验正常 Trap 流程中，恢复期间 SIE 保持关闭，
  最终由 sret 根据 SPIE 恢复中断使能。

Post-Condition：
- sstatus、sepc 分别从 status、epc 槽位恢复。
- x1、x3～x31 从各自槽位恢复，包括作为临时寄存器使用的 s0、s1。
- x2 从槽位 2 恢复，不能仅用固定栈增量猜测原始 sp。
- x0 保持硬连线零值。
- 宏结束后，已有 sret 可以依据恢复后的 CSR 返回。

Case 1：C 处理函数没有修改保存的通用寄存器。
返回后恢复进入 Trap 前的通用寄存器状态。

Case 2：C 处理函数合法修改了保存的寄存器、sp 或 epc。
按修改后的帧恢复，使处理函数对返回状态的调整生效。

Requirements：
- 先借用已保存的寄存器读取 status、epc 并写回 CSR，
  再恢复这些临时寄存器的原值。
- 保持 Trap frame 基址有效，完成全部依赖该基址的读取后，最后恢复 sp。
- 不在汇编恢复代码中统一给 epc 加 2 或加 4；是否前移及前移多少由异常处理层决定。
- 不把 badvaddr/cause 当作普通通用寄存器恢复。
- 不把 SPP 固定清零或置一，不强制返回某个特权级。

## __alltraps

Pre-Condition：
- 已进入四字节对齐的 __alltraps，已有 SAVE_ALL 建立完整 Trap frame。
- 输入内核栈满足 RV64 标准调用约定的 16 字节对齐要求。
- trap() 已由工程或独立测试提供，遵守相同调用约定。

Post-Condition：
- 传给 trap() 的 a0 等于当前 Trap frame 基址。
- trap() 返回时 sp 仍指向该帧，执行流进入已有 __trapret。
- 调用过程对 a0、ra 等寄存器的使用不影响最终帧恢复。

Requirements：
- 通过正常函数调用进入 trap()，使其能够返回。
- 不额外分配第二个 Trap frame，不破坏已有帧。
- 288 字节是 16 的整数倍，保持当前 RV64 场景下的栈对齐。


验证：
1. 静态对照 SAVE_ALL、struct trapframe 和 RESTORE_ALL，逐项核对偏移及恢复顺序。
2. 实际交叉编译本模块，使用 GNU99；新工具链不识别旧 CSR 名称 sbadaddr 时，
   仅在独立验证命令中使用 -Dsbadaddr=stval，不修改 SAVE_ALL。
3. 其他处理函数未实现时，使用隔离的 C trap() 测试函数验证本模块，
   不在正式 trap.c 中补写其他成员的函数以绕过依赖。
4. 分别触发 16 位 c.ebreak 和 32 位 ebreak：
   检查帧地址、16 字节栈对齐、全部保存的通用寄存器、cause、epc 和 sscratch；
   再由测试函数修改帧中的寄存器、sp、epc 和 sstatus，检查返回状态。
5. 验证不同 SPIE 输入对应的返回后 SIE 状态；
   区分架构规范与旧版 QEMU 的 SPIE 行为，不通过改动模块实现迎合模拟器。
6. 用有界 QEMU 运行并检查明确的成功日志，不能仅根据编译成功或超时判断通过。
7. 完整内核联调与 make grade 待其他成员的中断、异常处理完成后执行。
~~~~


## 提示词 2：内存管理基础恢复

~~~~markdown
[PROMPT]

任务：
为当前 ucore Lab3 工程恢复实验所需的 Lab2 内存管理前置代码。当前仓库已经使用 git 维护，lab2 即分支 lab2，lab3 即分支 lab3.

本轮主要修改：
- code/kern/mm/pmm.c：恢复 static void page_init(void)。
- code/kern/mm/best_fit_pmm.c：恢复 Best-Fit 内部状态、管理函数和管理器对象。

已有实现来源为本仓库的 Lab2 提交：
f53bf66（Complete report）。

这是已有代码的选择性迁移与适配任务，不是重新设计分配器，也不是完成全部 Lab3。

操作要求：
1. 先检查 git status、当前分支和相关文件。只能在 lab3 分支工作；若当前不是 lab3，停止修改并报告。保留所有已有改动。
   
2. 必须修改实际文件，不能仅输出代码片段。
   
3. 使用 git show 读取 f53bf66 中的对应源码，对照当前版本提取必要实现。不切换分支，不 merge、cherry-pick，也不整文件覆盖。
   
4. pmm.c 只迁入 page_init() 及其确实需要的局部依赖。保留当前 alloc_pages/free_pages/nr_free_pages 的中断保护，保留 pmm_init() 的现有顺序及页表地址记录。本轮保留当前默认 First-Fit 管理器选择。
   
5. best_fit_pmm.c 保留当前 basic_check()、best_fit_check() 的代码，补齐它们依赖的内部状态和分配器实现。即使当前未选用 Best-Fit，也不能通过排除该源文件绕过编译。

6. 不迁入 Buddy、SLUB、它们的专项测试或旧 Makefile 配置。不修改 struct Page、pmm_manager 的公共布局和已有页标志宏。

7. 不修改 trapentry.S，不实现 interrupt_handler()、exception_handler()，不修改启动逻辑或 tools/ 下的官方测试。不修改现有报告和提示词文档。

8. 保留已有任务标记与原实现署名，不猜测当前操作者的学号。

9. 未经我对每一次提交明确允许，不执行 commit、amend；不执行 push、reset、clean 或其他会丢弃工作区内容的操作。

输出要求：
- 列出迁移来源、实际修改范围及必要的适配。
- 区分“从 Lab2 迁回的代码”和“为 Lab3 做的调整”。
- 给出实际检查命令、结果与未完成的验证。
- 最后提供 git diff --stat 和剩余阻塞清单。
- 完成本轮后停止，不继续实现 Trap 处理。


[RELY]

以下事实来自本轮开始前的仓库，请核对实际代码后使用。

1. 当前初始化关系

kern_init() 先清零 BSS，再执行 dtb_init()。
pmm_init() 内依次执行：

init_pmm_manager();
page_init();
check_alloc_page();

之后记录启动页表的虚拟地址和物理地址。当前 init_pmm_manager() 选择 default_pmm_manager。

2. 可用迁移来源

使用以下命令读取旧版本：

git show f53bf66:code/kern/mm/pmm.c
git show f53bf66:code/kern/mm/best_fit_pmm.c

旧 pmm.c 还包含 Buddy、SLUB 及条件编译路径，这些内容不属于本轮迁移范围。

3. 当前地址定义，位于 memlayout.h

KERNBASE = 0xFFFFFFFFC0200000
KMEMSIZE = 0x7E00000
KERNTOP = KERNBASE + KMEMSIZE
PHYSICAL_MEMORY_OFFSET = 0xFFFFFFFF40000000

旧版本使用的 PHYSICAL_MEMORY_END，当前 memlayout.h 已没有定义。当前映射对应的物理排他上界可以由KERNTOP - PHYSICAL_MEMORY_OFFSET 得到。

不要直接复制一个依赖已删除宏的函数，也不要为解决这一处依赖恢复整个旧头文件。

4. 内存探测接口，位于 dtb.h

uint64_t get_memory_base(void);
uint64_t get_memory_size(void);

5. pmm.c 已有状态

struct Page *pages;
size_t npage;
uint64_t va_pa_offset;
const size_t nbase = DRAM_BASE / PGSIZE;

npage 表示物理页号的排他上界。pages[i] 对应物理页号 nbase + i。描述符数量为 npage - nbase。

已有：
static void init_memmap(struct Page *base, size_t n);

链接符号 end[] 表示内核结束位置的虚拟地址。

6. 当前页描述符和工具

struct Page {
    int ref;
    uint64_t flags;
    unsigned int property;
    list_entry_t page_link;
};

可用接口包括：
- PGSIZE、PGSHIFT、PPN；
- ROUNDUP、ROUNDDOWN；
- PADDR、pa2page、page2pa；
- set_page_ref；
- PageReserved、SetPageReserved；
- PageProperty、SetPageProperty、ClearPageProperty；
- 项目现有链表操作、assert、panic、cprintf。

当前页标志操作通过 atomic.h 实现，不要替换成旧版本的普通位操作。

7. Best-Fit 接口

pmm_manager 定义于 pmm.h，成员为：
name、init、init_memmap、alloc_pages、
free_pages、nr_free_pages、check。

best_fit_pmm.h 已声明：
extern const struct pmm_manager best_fit_pmm_manager;

当前 basic_check()、best_fit_check() 直接访问：free_list、nr_free。

需恢复与这些访问兼容的模块内部状态。

8. 已知验证限制

trap.c 中 interrupt_handler() 和 exception_handler() 尚未实现。因此本轮不能把完整工程编译或评分通过作为已完成的事实，也不能补空处理函数来绕过依赖。

现有报告记载的构建环境为 WSL CompilerLab，交叉编译器位于 /opt/riscv-elf-toolchains/bin/。这只是环境线索，实际使用前核对工具是否存在。


[GUARANTEE]

必须恢复：

static void page_init(void);

Best-Fit 模块内部管理函数：
static void best_fit_init(void);
static void best_fit_init_memmap(struct Page *base, size_t n);
static struct Page *best_fit_alloc_pages(size_t n);
static void best_fit_free_pages(struct Page *base, size_t n);
static size_t best_fit_nr_free_pages(void);

必须提供：
const struct pmm_manager best_fit_pmm_manager;

管理器的 check 成员连接当前已有 best_fit_check()。允许增加必要的模块内部 static 状态或辅助函数，不改变公共接口。


[SPECIFICATION]

## page_init

Pre-Condition：
- 启动映射已经建立，设备树解析已完成。
- 当前页管理器已经初始化，尚未登记真实空闲页面。

Post-Condition：
- 地址转换偏移、物理页号上界和描述符数组正确建立。
- 固件、内核、描述符及必要对齐空间保持保留。
- 剩余完整页面仅登记一次，登记范围不超出探测内存和映射范围。

Case 1：有效内存能够容纳内核、描述符及完整空闲页。
建立描述符，在实际写入数组前确认布局合法，再将描述符之后的完整页面交给当前管理器。

Case 2：内存信息无效或不能容纳必要布局。
使用现有 panic 报告原因；不能先向非法描述符范围写入数据再检查。

Case 3：必要布局能够容纳，但没有完整空闲页。
不登记空闲区，保持正确的描述符与保留状态。

Requirements：
- 使用当前工程的地址定义完成适配。
- 不混淆地址、页号、页数及描述符索引。
- 相关大小计算和对齐不能溢出。
- 不恢复旧版 pmm.c 中缺少中断保护的分配接口。


## Best-Fit 管理器

Pre-Condition：
- 使用当前工程的 Page、链表和管理器接口。
- 当前自检函数保持不变。

Post-Condition：
- 管理器状态、函数及注册对象完整，能够独立编译。
- 初始化后空闲链表为空、计数为零。
- 分配选择最小的可满足块，正确处理切分。
- 释放后保持地址有序，合并相邻空闲块，计数准确。
- 分配失败不改变已有空闲状态。
- check 接口调用当前框架自检。

Requirements：
- 优先复用指定 Lab2 提交的已有实现。
- 不因当前默认选择 First-Fit 而省略 Best-Fit。
- 不改动已有测试断言来适配迁移结果。


## 本轮验证

1. 对照迁移前后差异，确认只修改两个目标文件，没有带入 Buddy、SLUB 或旧构建配置。

2. 核对 page_init() 的内存布局、地址上界和描述符数量，确认不再引用当前不存在的地址宏。

3. 在可用交叉编译环境中，实际编译两个内存模块。可核对 Makefile 后尝试目标：
   obj/kern/mm/pmm.o
   obj/kern/mm/best_fit_pmm.o

4. 编译结果必须来自本轮源码，避免复用旧对象。工具不可用或编译失败时，记录具体原因。

5. 本轮不要求完整内核评分。明确说明两个 Trap 处理函数仍缺失；对象编译通过不能表述为内核启动或分配器运行测试通过。

6. 检查最终 git diff，确认当前分支和已有改动得到保留，没有执行任何提交。

~~~~


## 提示词 3：中断与异常处理

~~~~markdown
[PROMPT]

任务：
完成 ucore 实验 Lab3 的中断与异常处理模块。

在 code/kern/trap/trap.c 的
“//lab3 function2 YOUR CODE”位置实现：
- interrupt_handler()
- exception_handler()

目标是让现有 Trap 入口能够完成具体处理，再通过已经实现的上下文恢复返回；正常启动后能够持续接收时钟中断，周期输出统计信息，并在达到约定次数后自动关机。

操作要求：
1. 必须修改实际文件，主要修改范围仅为 trap.c。允许增加必要的 #include、文件内部宏、static 状态和辅助函数。

2. 先检查当前分支和工作区。必须在 lab3 分支工作，可以使用 git 查看已提交的改动。

3. 不修改 trapentry.S、SAVE_ALL、Trap frame 布局。不修改现有 trap()、trap_dispatch()、print_ticks()、print_trapframe() 或其他已有函数。

4. 不修改内存管理模块、启动逻辑、时钟驱动、SBI 实现、Makefile 或 tools/ 下的官方测试和注入脚本。如构建发现范围外问题，报告具体位置和证据，不通过删源码、关警告、改测试等方式绕过。

5. 不提前编写完整专项测试套件，不修改报告正文。可以在隔离的临时副本中进行必要的基本验证；必须区分测试替身与正式内核路径。

6. 保留 lab3 function2 标记。当前操作者的学号尚未明确，不猜测署名，本轮保留 YOUR CODE，作为最终整理待办。

7. 未经我明确允许，不执行 commit、amend；不执行 push，不切换分支，不丢弃已有改动。

输出要求：
说明中断原因提取、时钟重设与计数、统计及关机条件、异常指令长度判断和 EPC 更新方式。列出实际构建与基本运行结果，保留关键命令和日志。区分已验证、未验证以及范围外阻塞。

[RELY]

以下是当前工程事实，开始前核对真实定义。

1. 调用关系

__alltraps
→ SAVE_ALL
→ trap(struct trapframe *tf)
→ trap_dispatch(tf)
→ interrupt_handler(tf) 或 exception_handler(tf)
→ RESTORE_ALL
→ sret

trap_dispatch() 已通过 tf->cause 的符号位区分中断与异常，但传给处理函数的 cause 仍是原始值。
汇编入口和上下文恢复已经实现，不属于本轮重写范围。

2. Trap frame，定义于 trap.h

struct trapframe {
    struct pushregs gpr;
    uintptr_t status;
    uintptr_t epc;
    uintptr_t badvaddr;
    uintptr_t cause;
};

uintptr_t 在当前工程中为 64 位。epc 是返回执行位置，异常处理对该字段的调整会由恢复汇编生效。

3. 当前 trap.c

#define TICK_NUM 100

static void print_ticks() {
    cprintf("%d ticks\n", TICK_NUM);
#ifdef DEBUG_GRADE
    cprintf("End of Test.\n");
    panic("EOT: kernel seems ok.");
#endif
}

已有：
void print_trapframe(struct trapframe *tf);

保持 print_ticks() 原样。正常运行和官方 Lab3 评分不额外启用 DEBUG_GRADE。

4. 时钟接口，定义于 clock.h

extern volatile size_t ticks;
void clock_init(void);
void clock_set_next_event(void);

clock_init() 设置首个时钟事件并清零 ticks。后续每次事件需要处理函数重新设置。驱动的事件间隔为 timebase=100000，使用 SBI 设置定时器。

5. 关机接口，定义于 sbi.h

void sbi_shutdown(void);

当前框架未定义 SHUTDOWN_AFTER。本轮采用项目暂定配置：
SHUTDOWN_AFTER = 10

其单位是“统计输出次数”，不是时钟中断次数。TICK_NUM=100 时，正常运行在第 1000 次时钟中断输出第十行统计后关机。
这是本项目的明确设计选择，不声称该数值来自老师规定。

6. 原因宏，定义于 riscv.h

IRQ_U_SOFT、IRQ_S_SOFT、IRQ_H_SOFT、IRQ_M_SOFT
IRQ_U_TIMER、IRQ_S_TIMER、IRQ_H_TIMER、IRQ_M_TIMER
IRQ_U_EXT、IRQ_S_EXT、IRQ_H_EXT、IRQ_M_EXT

其中 IRQ_S_TIMER 为 5。

异常：
CAUSE_ILLEGAL_INSTRUCTION = 2
CAUSE_BREAKPOINT = 3

中断编码最高位是中断标志，其余位表示原因。提取原因时不能丢失原始 tf->cause 或其他帧字段。

7. 当前官方测试的可观察要求

- 断点输出包含 “Breakpoint at”。
- 非法指令输出包含 “Illegal instruction at”。
- Supervisor 软件中断输出包含 “Supervisor software interrupt”。
- 未知中断原因 15 输出包含 “Unknown interrupt: cause 15”，并输出完整 Trap frame。
- 其他异常调用 print_trapframe()；原因 20 的现有输出包含 “cause    0x00000014”。
- 时钟统计输出 “100 ticks”。

官方非法指令用例是四字节的 .word 0x0000007b。断点汇编中的 ebreak 可能被编码为两字节压缩指令，不能根据汇编助记符直接假定长度。
官方时钟测试等待 ticks 达到 200 后打印成功标记。它本身不验证完整的自动关机条件。

8. 当前前置代码与环境

pmm.c、best_fit_pmm.c 已完成上一轮迁移，且已提交。默认管理器仍为 First-Fit。

上一轮实际可用环境：
- WSL Ubuntu。
- /opt/riscv/bin/riscv64-unknown-elf-gcc，版本 10.2.0。

QEMU 的位置和版本需要重新检查，不能直接照抄旧报告中的 CompilerLab 路径。


[GUARANTEE]

必须提供外部可调用的两个函数：

void interrupt_handler(struct trapframe *tf);
void exception_handler(struct trapframe *tf);

不能声明为 static，官方测试会直接调用。允许增加本文件内部的 static 辅助函数、状态和必要宏，不改变任何已有公共接口。


[SPECIFICATION]

## interrupt_handler

Pre-Condition：
- tf 指向有效的当前 Trap frame。
- 正常入口传入带中断标志的 cause。
- 官方直接调用测试也可能传入不带该标志的原因编号。
- 当前为单 hart，不要求处理函数主动开启嵌套中断。

Post-Condition：
- 根据去除中断标志后的原因完成分类。
- 除时钟计数和必要的模块内部统计状态外，不改变被打断程序的保存状态。
- 中断返回位置 epc 保持不变。

Case 1：Supervisor 时钟中断。
每次重新安排下一事件，ticks 恰好增加一次。更新后达到 TICK_NUM 的正整数倍时，通过原有 print_ticks() 输出一次统计。累计输出达到 SHUTDOWN_AFTER 后，先完成该次统计输出，再调用 sbi_shutdown()。第 1 到第 999 次中断不得提前触发正常关机。

Case 2：其他已定义的软件、时钟或外部中断。
输出能够区分权限级别与中断类别的提示；Supervisor 软件中断使用约定文本。不增加 ticks，不触发正常时钟关机逻辑，不改变 epc。
本实验不扩展外设驱动或实现通用设备中断恢复。

Case 3：未识别的中断原因。
输出 “Unknown interrupt: cause <十进制原因编号>”，随后调用 print_trapframe(tf)，保留原始 Trap frame。

Requirements：
- 下一次事件安排不能只在初始化时发生。
- 不清零或回绕 ticks 来实现周期输出。
- 不为适应评分加入特殊的伪成功输出或跳过路径。
- 不修改现有 DEBUG_GRADE 行为；将其提前 panic 与正常自动关机明确区分。


## exception_handler

Pre-Condition：
- tf 指向有效 Trap frame，cause 为异常原因。
- 对本轮要求跳过的非法指令和断点，epc 指向可读取的内核指令地址。
- 本轮指令长度支持范围为 RV64 平台的 16 位和 32 位指令；不要求实现用户地址安全访问或长指令解码器。

Post-Condition：
- 非法指令与断点输出原始异常地址，按实际编码长度推进保存的 epc。
- 其他通用寄存器、status、cause、badvaddr 保持不变。
- 处理完成后可以衔接现有 Trap 返回流程。

Case 1：非法指令异常。
输出 “Illegal instruction at 0x<地址>”。对于可识别的两字节或四字节编码，epc 分别前移 2 或 4。四字节官方用例必须返回其后一条指令。

Case 2：断点异常。
输出 “Breakpoint at 0x<地址>”。压缩断点与普通断点分别按两字节和四字节处理，返回后继续执行后续指令，不能再次陷入同一断点。

Case 3：其他异常，或本轮未支持的更长指令编码。
调用 print_trapframe(tf) 输出诊断，不随意修改 epc。这属于兜底诊断，不承诺能够从未知异常恢复。

Requirements：
- 不能为所有异常统一加 4，也不能为所有断点统一加 2。
- 从指令开头读取编码信息时考虑两字节对齐；不依赖可能未对齐的四字节读取。
- 根据指令长度编码区分 16 位、32 位和更长编码，不把所有低两位为 11 的编码都宣称为四字节。
- 不将 badvaddr 一概当作可用的指令内容。
- 指令地址必须按当前内核虚拟地址处理，不截断为 32 位，不重复转换为物理地址。


## 本轮基本验证

1. 检查实际 diff：本轮正式源码只新增 trap.c 修改，前一轮两个内存文件及队友文件没有被改写。

2. 实际构建完整内核。使用当前可用工具链，说明实际命令与兼容参数。若工具链或范围外代码阻塞构建，保留错误证据。

3. 条件允许时，按原有流程运行 make grade。不修改官方测试和注入脚本。注意评分脚本直接调用 qemu-system-riscv64，仅设置 make 的 QEMU 变量未必能影响评分；应核对实际 PATH 中的可执行文件。

4. 如评分运行，保存总分及每个子项的真实日志，同时检查 FAIL、panic 和重复异常。单凭成功字符串或超时不能证明全部行为正确。

5. 不带评分宏运行正常内核：检查内存初始化与原有分配器自检通过，持续输出十行 “100 ticks”，随后由内核主动关机。使用有界等待，区分 SBI 关机退出与外部超时终止。如无法实际运行，明确列为未验证。

6. 若基本验证中发现当前范围内的实现问题，给出失败现象、原因、修复及复验结果。
   两种长度断点、非法指令边界和更完整的状态保持测试，留待下一轮专项验证。

本轮交付实现与真实基本验证结果，不提交代码，
不扩展实验内容，不提前整理完整报告。
~~~~


## 提示词 4：启动映射与基本验证

~~~~markdown
[PROMPT]

任务：
修复当前 ucore Lab3 正式源码的启动映射缺口，建立可复现的本地运行方式，并验证正式源码的启动、持续时钟中断、自动关机及官方评分。

本轮明确允许修改：
- code/kern/init/entry.S：修复启动页表缺少低地址恒等映射的问题。
- report/validation/boot/：新增本轮验证脚本、说明和必要日志。

本轮目标是将上一轮只在副本成立的验证结果，落实到正式源码，并保留可复核证据。

操作要求：
1. 先核对分支、HEAD 和工作区。当前预期在 lab3，HEAD 为 8108993，trap.c 存在上一轮尚未提交的实现。以实际状态为准，保留已有改动。

2. 必须修改真实文件。entry.S 只做启动映射所需的最小修复，同步纠正因此失效的附近注释。不整文件覆盖旧版 entry.S。

3. 可以读取本仓库 f53bf66 中的 entry.S，但只迁回所需页表映射。保留当前保存 hartid/DTB、设置精确栈顶、跳转 kern_init 等现有逻辑。

4. 不修改 trap.c、trapentry.S、内存管理代码、时钟驱动、SBI 实现、链接脚本或 Makefile。不修改 tools/ 下的官方测试和注入脚本。

5. 本轮新增的验证脚本放入 report/validation/boot/，不把必需步骤只保存在仓库外的个人目录中。
   不改写队友已有的 report/validation/trapentry/，暂不修改实验报告正文。

6. 可以提供用于当前 QEMU 的验证适配器，但必须明确其参数转换范围和实际调用的模拟器。不伪造日志、退出码或测试结果。不把适配器描述为官方原始运行环境。

7. 不依赖上一轮影子树中的源代码补丁。如使用临时验证副本，必须从本轮正式源码重新创建，内核源码不得再加补丁；官方测试注入另行说明。

8. 本轮不处理未知中断编号输出截断等其他问题，不新增完整 Trap 专项测试，不扩大功能范围。

9.  在 lab3 工作，不切分支。未经我明确允许，不执行 commit、amend；不执行 push，不丢弃已有改动。

输出要求：
- 解释开启分页后为何仍需要低地址映射。
- 说明两个有效根页表项的索引、映射范围和权限。
- 列出实际修改、构建、运行与评分结果。
- 将“正式源码结果”“参数适配”“官方测试注入”分开说明。
- 给出可重新执行的验证入口、日志位置及剩余问题。
- 完成本轮后停止。


[RELY]

以下是已核对的工程事实，开始前复核当前代码。

1. 当前启动过程

code/kern/init/entry.S：
- 在物理地址附近开始执行。
- 保存 hartid 和 DTB 指针。
- 计算 boot_page_table_sv39 的物理页号。
- 写入 satp，开启 Sv39。
- 执行 sfence.vma。
- 设置高地址栈，跳到高地址 kern_init。

当前反汇编中：csrw satp 位于对应物理地址 0x80200034，其后 sfence.vma 位于 0x80200038。跳到高地址以前，PC 仍处于低地址区间。

2. 当前根页表

boot_page_table_sv39 按 4096 字节对齐，共有 512 个八字节页表项。
当前前 511 项为空，索引 511 的有效叶项将高地址窗口映射到 DRAM：
.quad (0x80000 << 10) | 0xcf

当前缺少低地址 DRAM 窗口的恒等映射。

3. Sv39 相关定义

根级索引由地址的 bits[38:30] 决定。根级叶项可以映射 1 GiB，物理基址必须满足对应对齐。

低地址窗口：
[0x80000000, 0xc0000000)

高地址窗口：
[0xffffffffc0000000, 0x10000000000000000)

二者应映射到同一段物理窗口：
[0x80000000, 0xc0000000)

高地址区间的排他上界是数学意义上的 2^64，不能在程序中用一个普通 uint64_t 值表示该上界。
本轮沿用现有教学启动映射及权限，不引入页表重设计。

4. 已有修复来源

git show f53bf66:code/kern/init/entry.S

Lab2 在根页表索引 2 处建立了 DRAM 恒等映射，并保留索引 511 的高地址映射。
需要保留根表总长度 4096 字节，不能因为添加一项而移动后续布局或破坏对齐。

5. 当前启动失败证据

上一轮日志：
C:\Users\lenovo\AppData\Local\Temp\lab3_trap_verify\
lab3_startup_page_fault.log

最初的取指缺页：
epc=0x80200038，tval=0x80200038。

后续反复出现在 0x80200000 的异常属于后续故障现象，不要把它误写成最初故障指令地址。

6. 当前实现状态

- page_init() 与 Best-Fit 已迁回，提交为 8108993。
- trap.c 已实现两个处理函数，但尚未提交。
- 默认管理器仍为 First-Fit。
- TICK_NUM=100，SHUTDOWN_AFTER=10。
- 正常运行应在第十次统计输出后调用 sbi_shutdown()。
- 队友完成的 trapentry.S 必须保持不变。

7. 已知环境

上一轮实际使用：
- WSL Ubuntu。
- /opt/riscv/bin/riscv64-unknown-elf-gcc，GCC 10.2.0。
- /opt/qemu/bin/qemu-system-riscv64，QEMU 7.0.0。

当前 Makefile 使用：
-device loader,file=bin/ucore.img,addr=0x80200000

上一轮在该 QEMU/OpenSBI 组合中，此方式没有建立正确的下一阶段入口。改用 -kernel bin/ucore.img 后，才能观察到内核启动映射故障。
bin/ucore.img 是原始二进制镜像；不要将高虚拟地址链接的 bin/kernel ELF 不加分析地当作等价装载输入。

8. 官方评分方式

make grade 会复制工程、调用 tools/inject.py，再运行 tools/grade.sh，最后清理评分临时目录。grade.sh 直接调用 PATH 中的 qemu-system-riscv64；仅设置 make 的 QEMU 变量不能保证影响评分。

上一轮评分依赖仓库外的 QEMU 参数适配器。本轮如继续使用，应将必要适配逻辑保存为本轮验证材料，而不是依赖那个未归档的个人脚本。


[GUARANTEE]

必须完成：
- 正式 entry.S 中最小的启动页表修复。
- 可重复执行的正常启动验证入口。
- 当前环境下运行官方评分的准确步骤。
- 实际命令、工具版本、退出状态与关键日志记录。

本轮没有新增 C 公共接口。

允许新增验证目录内的脚本和说明；这些脚本不能修改正式内核源码或官方测试。


[SPECIFICATION]

## 启动页表修复

Pre-Condition：
- 内核从 DRAM 中的低地址入口开始执行。
- 根表按页对齐。
- 当前高地址映射已经存在。

Post-Condition：
- 开启分页后，低地址执行流可以继续取指。
- 跳转高地址后，内核代码、栈和数据仍按原有偏移访问。
- 根表保持 512 项、4096 字节，原高地址映射不变。
- 其他启动逻辑和队友实现保持不变。

Case 1：开启 satp 后，尚未跳到高地址。
低地址 PC 通过恒等映射指向原物理指令，可以执行后续刷新、栈设置和跳转。

Case 2：进入高地址 kern_init。
高地址映射继续生效，DTB、控制台、物理内存初始化和分配器自检能够继续执行。

Requirements：
- 核对两个有效叶项的根表索引和物理基址。
- 不建立超过本轮所需的其他有效项。
- 不通过关闭分页、移动链接地址或绕过初始化解决问题。
- 解释修复依据，而不是仅声称“照搬 Lab2 后能运行”。


## 本地 QEMU 运行方式

Pre-Condition：
- 已核对实际模拟器路径与版本。
- 正式源码已经重新构建。

Post-Condition：
- 运行过程使用本轮正式源码生成的镜像。
- 镜像装载与入口设置适用于实际 QEMU/OpenSBI 组合。
- 参数适配和原始执行命令均可复核。

Requirements：
- 适配器只处理本项目需要的装载参数形式；
  对其他不支持形式明确报错或保持原样，不默默猜测。
- 原样传递评分宏以外的运行参数、串口输出和退出状态。
- 可通过参数或任务专用环境变量指定真实 QEMU，
  避免把个人绝对路径作为唯一可执行方式。
- 如使用适配器，明确称为“参数适配下运行官方评分”。


## 验证与证据

1. 构建与静态检查：重新构建当前正式源码，避免复用旧镜像。检查根表长度、有效项索引和反汇编中的启动流程。运行 git diff --check。

2. 正常内核不启用评分宏或 DEBUG_GRADE。有界运行，保存退出码、串口日志和实际命令。
日志应显示：
- DTB 与物理内存初始化；
- check_alloc_page() succeeded!；
- 十行独立的 “100 ticks”；
- 无内核 panic、测试 FAIL 或重复异常循环。

正常关机应发生在超时以前。不能只凭 QEMU 返回 0 就声称内核完成全部流程，还必须核对上述日志和实际运行方式。

3. 官方评分：使用本轮正式源码，通过原有 make grade 目标运行。如使用 QEMU 参数适配器，记录准确的 PATH 设置与转换规则。保存评分输出和退出状态。
提前安排子项日志留存：官方目标会删除临时评分目录，不能把已被删除的日志列为已归档材料。如需在源码一致的临时副本直接调用 inject.py 与 grade.sh，明确记录该入口及其与 make grade 的关系。
评分副本除了官方注入，不得再修改启动或 Trap 实现。目标为三个子项通过、总分 100/100，同时检查实际日志中有无失败和异常重复。

4. 证据归档，在 report/validation/boot/ 保存：
- 验证脚本与简短使用说明；
- 实际工具版本和运行命令；
- 正常运行与评分的必要文本日志；
- 实际退出状态；
- 本轮验证所对应源码的内容指纹或等价记录。

对于上一轮影子树日志，保留其历史来源说明，不要改名后当成本轮正式源码的验证结果。

5. 最终范围核对：确认本轮只新增 entry.S 的必要修改和验证材料。上一轮 trap.c 改动仍完整保留，队友汇编、报告和官方工具没有改变。不执行提交。

本轮完成后给出结果与剩余待办。Trap 指令长度边界、状态保持以及未知中断大编号，留待下一轮专项验证。
~~~~


## 提示词 5：专项检查与编号输出修复

~~~~markdown
[PROMPT]

任务：
为当前 ucore 实验 Lab3 增加独立的 Trap 专项验证，覆盖不同指令长度、Trap frame 状态保持、中断分类及精确的时钟关机边界。同时修复 trap.c 中未知中断原因输出的整数截断问题。

本轮主要新增：
report/validation/trap/

正式源码允许的修改：
code/kern/trap/trap.c 中未知中断原因的输出语句。

操作要求：
1. 先检查当前分支、提交及工作区。在 lab3 分支工作，保留已有 entry.S、trap.c 改动，以及 report/validation/boot/ 验证材料。如果这些内容已经提交，以实际状态为准，不撤销提交。

2. 必须新增实际可运行的验证文件，不能只提供测试方案。

3. 专项测试集中放在 report/validation/trap/，不修改 tools/ 下的官方测试，不修改正式 Makefile、init.c、trapentry.S、trap.h，不修改内存管理、时钟或 SBI 实现。

4. 修复未知中断输出时保留完整的原因编号，不改变原因提取、分类和 Trap frame 内容。其他正式实现原则上保持不变。如果测试发现其他缺陷，先给出失败证据和原因，本轮停止扩展修复，交回我复核。

5. 测试必须调用实际 trap.c 的公共处理函数，不能复制一份处理逻辑当作被测实现，不能通过调用 instruction_length() 等私有辅助函数代替公共行为验证。

6. 允许在隔离测试中替换控制台、时钟设置和关机接口，用于记录调用、输出及顺序。必须明确这些是测试替身，其结果不代表真实硬件行为。

7. 真实 Trap 返回测试必须使用正式 trapentry.S 和 trap.c。如需修改测试副本的启动入口或构建接入，修改只能发生在新建临时副本中，并保留准确的注入脚本与差异说明。

8. 沿用当前可用环境 QEMU；不调整现有参数适配器。不修改现有报告正文和历史验证日志。新结果放在本轮目录，注明对应源码指纹。不通过覆盖旧日志使历史记录看起来对应新源码。

9. 未经明确允许禁止提交 git。本轮不 commit、amend、push，不切分支，不删除或回退已有工作。

输出要求：
给出测试入口、逐项断言、实际结果和关键日志，并明确区分：
- 直接调用处理函数的测试；
- 经真实 Trap 入口与 sret 的测试；
- 真实时钟与关机的集成检查。

解释每项测试能发现什么错误，以及仍未覆盖的范围。


[RELY]

1. 当前公共函数

void interrupt_handler(struct trapframe *tf);
void exception_handler(struct trapframe *tf);
void trap(struct trapframe *tf);

处理函数虽未在 trap.h 声明，独立测试可使用匹配的 extern 声明，不必为本轮修改正式头文件。

2. 当前 Trap frame

struct trapframe {
    struct pushregs gpr;
    uintptr_t status;
    uintptr_t epc;
    uintptr_t badvaddr;
    uintptr_t cause;
};

uintptr_t 在本工程中为 64 位。

3. 当前处理行为

- 中断原因来自 cause 去掉最高位后的值。
- 已知的其他中断只输出分类信息。
- 未知中断输出原因并调用 print_trapframe()。
- Supervisor 时钟中断重设事件、ticks 加一。
- 每 100 次中断输出一次 “100 ticks”。
- 累计十次统计后调用 sbi_shutdown()。
- 非法指令和断点根据编码长度推进 epc。
- 其他异常只输出 Trap frame，不改变 epc。
- 不支持的长指令编码只诊断，不推进 epc。

4. 已知缺陷

当前未知中断分支为：

cprintf("Unknown interrupt: cause %d\n", (int)cause);

较大的原因编号会在转为 int 时截断。必须根据工程实际整数类型与格式化器支持，输出完整的无符号十进制原因编号。
例如原因 0x10000000f 的十进制值为 4294967311，不能输出成 15。
原始 tf->cause 必须保持不变。

5. 编码与测试范围

压缩断点：
0x9002，2 字节。

普通断点：
0x00100073，4 字节。

官方非法指令用例：
0x0000007b，4 字节。

全零的 16 位编码：
0x0000，为非法指令。

长度编码前缀：
- 低两位不为 11：16 位；
- 低两位为 11，且 bits[4:2] 不全为 1：32 位；
- 其余属于本轮不支持的更长编码。

未知或长编码兜底只做直接调用测试，不要让真实执行流停在不推进 epc 的异常上形成循环。

6. 当前已有证据

report/validation/trapentry/：
队友的上下文恢复独立测试，保持不变。

report/validation/boot/：
正常启动、十次统计关机及官方评分验证。

这些测试已有覆盖，不要求重写。本轮只补足缺失行为，并对源码修复做必要回归。


[GUARANTEE]

必须交付：
- 未知中断完整原因编号输出修复；
- 独立专项测试源码；
- 自动运行及判断结果的入口；
- README，说明测试分层、替身、运行方法与限制；
- 真实日志及被测源码指纹。

不增加正式内核公共接口，不为测试暴露私有状态或增加正式重置函数。


[SPECIFICATION]

## 未知中断编号修复

Pre-Condition：
tf 为有效 Trap frame，原因编号不是已知中断。

Post-Condition：
输出完整原因编号和 Trap frame；所有保存字段保持不变。

Case 1：原因编号 15。
保留官方要求的文本：
Unknown interrupt: cause 15

Case 2：原因编号 0x10000000f。
输出：
Unknown interrupt: cause 4294967311

Case 3：中断标志存在或不存在。
在处理函数直接调用契约下，两者分类一致；打印帧时仍保留输入的原始 cause。

Requirements：
不能通过压缩成 int、截取低位或有符号误解释丢失编号。


## 处理函数直接调用测试

Pre-Condition：
使用实际 trap.c；为测试数据建立有效、可读取、两字节对齐的指令区域；Trap frame 的各字段填入可区分的确定值。

Post-Condition：
每个用例比较处理前后的完整帧，只允许契约规定的 epc 变化。

Case 1：两种原因、两种长度。
分别对非法指令和断点原因提供 16 位与 32 位编码，检查 epc 精确增加 2 或 4。至少有一项 32 位指令位于地址模 4 等于 2 的位置。

Case 2：不支持的长编码。
选择低五位为 11111 的代表编码，检查诊断发生，epc 与其他字段均不变。这只验证处理函数的兜底策略，不实际执行该编码。

Case 3：其他异常。
例如 cause=20，检查输出诊断且整帧不变。可以给 epc 一个不作为可读指针使用的哨兵值，确认其他异常分支不尝试读取指令。

Case 4：其他已知中断。
覆盖工程定义的软件、外部及非 Supervisor 时钟类别，检查类型输出、整帧不变、ticks 不变，且不调用时钟重设或关机替身。

Case 5：未知中断。
覆盖原因 15 和 0x10000000f，分别带中断标志和使用裸原因直接调用。检查输出、完整帧不变及无时钟副作用。

Requirements：
- 状态比较须覆盖 gpr、status、epc、badvaddr、cause。
- 不只检查“没有崩溃”或某个 PASS 字符串。
- 使用已初始化数据，避免拿结构体未初始化填充字节作证据。
- 替身输出捕获应有长度边界，不能静默截断关键诊断。


## 时钟精确边界测试

Pre-Condition：
独立、全新启动的测试实例；ticks 和被测模块统计状态来自正常初值。以记录调用的替身代替时钟设置与 SBI 关机。

Post-Condition：
连续处理第 1 至第 1000 次 Supervisor 时钟中断：
- 每次重设事件一次；
- 每次 ticks 恰好加一；
- 输入 Trap frame 不变；
- 第 100、200……1000 次各输出一次统计；
- 第 1 至第 999 次不调用关机；
- 第 1000 次完成第十次统计后调用关机一次。

Requirements：
- 检查逐次边界，不只比较最终计数。
- 记录第 1000 次的输出与关机先后关系。
- 不修改或直接重置 tick_reports 私有状态。
- 不通过把 ticks 直接设为 999 跳过前九次统计。
- 关机替身可返回以便断言，但不继续测试关机后的行为。
- 该测试证明处理逻辑，不能代替真实定时器与关机验证。


## 真实 Trap 返回补充测试

Pre-Condition：
使用正式 trapentry.S 与 trap.c，测试代码从独立临时副本或隔离镜像接入，不修改正式初始化文件。

Post-Condition：
显式触发以下指令后均能继续执行，并到达各自独立的后续标记：
1. 16 位压缩断点；
2. 32 位普通断点；
3. 16 位非法指令；
4. 32 位非法指令。

至少一条 32 位指令放在两字节而非四字节对齐地址。

Requirements：
- 显式固定指令编码，不仅依赖 ebreak 助记符。
- 保留反汇编，核对实际编码、地址及下一条指令位置。
- 关键寄存器或栈哨兵在 Trap 前后保持。
- 若用汇编函数从 C 调用，测试本身必须遵守调用约定：返回前恢复它改写的 callee-saved 寄存器，保留正确的 ra 和 sp。
- 不将返回后的寄存器差异误归因于 Trap，而忽略测试函数自己的 ABI 破坏。
- 不重写队友已有的全寄存器测试；本轮重点补齐长度组合与真实返回。
- 不触发不支持的长编码或未知异常恢复循环。


## 执行、证据和回归

1. 给每项测试独立结果，断言失败返回非零。总成功标记只能在所有断言通过后输出。

2. QEMU 运行有超时上限。结合进程状态、完整标记与失败日志判断结果，超时本身不能作为成功证据。

3. 源码修复前先运行大编号输出用例，保存失败现象；修复后重新运行并保存通过证据。测试失败时不要提前覆盖修复前日志。

4. 测试独立验证目录与临时目录分工明确，临时注入不能残留到正式 code/ 中。不复制旧构建产物来代替重新构建。

5. 专项验证通过后，运行已有正常启动验证与一次官方评分回归，沿用当前 QEMU 与参数适配方式。新结果保存到本轮目录，不覆盖上一轮归档结果。

6. 最终检查 git diff：正式源码新增修改仅限未知中断输出修复；entry.S 及其余既有实现保持本轮开始时的内容。

本轮结束给出：
- 测试结果与实际覆盖；
- 修复前失败、修复后通过的证据；
- 是否存在其他实现缺陷；
- 当前源码与历史证据的对应关系；
- 仍未覆盖的范围。

不提交代码，不更新实验报告正文。
~~~~

