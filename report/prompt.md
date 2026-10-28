# Lab3 提示词——石晨昊负责部分

本文件采用课程模板的 [PROMPT]、[RELY]、[GUARANTEE]、[SPECIFICATION] 四部分结构，仅包含 2412449-石晨昊负责的功能模块一。以下是依据实际任务与实现过程整理的定稿提示词，不是原始对话的逐字记录。实现说明与验证结果见 [report.md](report.md)。

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
