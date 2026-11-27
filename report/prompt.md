# 小组实验提示词记录

本文件沿用课程的 `[PROMPT]`、`[RELY]`、`[GUARANTEE]`、`[SPECIFICATION]` 四段式结构。以下是根据指导书、Lab5 源码、Git 提交和本轮验证整理的可复用提示词。模块一、二以提交 `48cb0a2` 中的实现为依据，联调记录对应实际观测到的现象。

## 提示词 1 用户态系统调用

~~~~text
[PROMPT]
实现 Lab5 模块一，在 kern/trap/trap.c 中接收 U 态 ecall，在 kern/syscall/syscall.c 中解析编号、传递参数和写回结果。检查 trapentry.S 的保存与恢复路径能否同时服务内核态和用户态。保留 Lab5 模块标记与已有公开 ABI。

[RELY]
- user/libs/syscall.c 规定 a0 为系统调用号，a1 到 a5 为实参，调用后 a0 为结果。
- libs/unistd.h 给出 SYS_exit、SYS_fork、SYS_wait、SYS_exec、SYS_yield、SYS_kill、SYS_getpid、SYS_putc、SYS_pgdir。
- trapframe 保存 pushregs、status、epc、tval 和 cause，current->tf 指向当前 trapframe。
- 进程控制由 do_* 接口承担。用户指针由 do_wait 和 do_execve 用 VMA 和实际 PTE 校验。
- trapentry.S 使用 sscratch、SPP、SPIE 和 sret 恢复执行现场。

[GUARANTEE]
exception_handler(struct trapframe *tf) 与 syscall(void) 保持现有签名。分发入口只在用户 ecall 时推进系统调用的返回 PC。合法请求执行对应内核入口并得到可见的返回值。未知调用返回 -E_INVAL。

[SPECIFICATION]
前置：内核已保存 U 态寄存器，用户库和内核使用同一组 SYS_* 定义。
1. cause 为 CAUSE_USER_ECALL 时，先将 tf->epc 加 4，再调用 syscall。
2. 读取 a0 为编号，从 a1 到 a5 复制参数。调用前检查编号范围和分发表槽位。
3. 通过封装函数调用内核接口。结果写回保存的 a0，供 sret 后的用户代码读取。
4. 从 U 态陷入时换入内核栈并保存原 sp。回到 U 态前重新登记内核栈并恢复用户 sp。从 S 态陷入时继续在当前内核栈保存现场。
5. 通过 test_lab5_syscall() 验证 SYS_getpid、非法编号和 EPC 前移，再用运行中的用户程序核对真实 ecall 的参数与返回值。
~~~~

## 提示词 2 首个用户进程的创建与装载

~~~~text
[PROMPT]
实现 Lab5 模块二，在 kern/process/proc.c 中闭合从 proc_init 到首个用户程序运行、退出和回收的路径。核对 alloc_proc、kernel_thread、init_main、user_main、kernel_execve、do_execve、load_icode 和 do_wait 的职责。

[RELY]
- proc_struct 保存内核栈、trapframe、mm、页表根、父子进程关系和退出码。
- Makefile 将用户 ELF 链入内核。KERNEL_EXECVE 和 KERNEL_EXECVE2 提供镜像起点和长度。
- ELF 使用 elfhdr、proghdr 和 PT_LOAD。VMA 使用 mm_map，映射使用 pgdir_alloc_page。
- forkrets 通过 __trapret 和 sret 恢复现场。USTACKTOP 为用户栈顶。
- do_wait 使用 find_proc、cptr/optr、WT_CHILD、PROC_ZOMBIE 和进程树清理接口。

[GUARANTEE]
默认程序或 TEST 指定的 ELF 可以启动到 U 态。装载后的用户页、栈和入口地址来自有效的 ELF 元数据。父进程可以等待并回收其子进程。

[SPECIFICATION]
前置：内核已建立内核页表和调度器，嵌入的 ELF 可从链接符号读取。
1. 创建 idleproc 和 initproc，再由 init_main 创建 user_main。内核线程首次进入时保留 S 态现场。
2. 核对 ELF 头、程序头表、文件偏移、段大小和用户地址范围。按 PT_LOAD 的 R/W/X 标志建立 VMA 与用户页表映射。
3. 页内容先清零再复制文件字节。用户栈建立 VMA，预分配栈顶四页。检查入口属于可执行 VMA。
4. 更新 mm 与页表根，设置 epc、用户 sp，清除 SPP 并设置 SPIE。kernel_execve 从 forkrets 进入 trap 返回路径。
5. 用户 exec 的名字和镜像来自旧地址空间，先核对并暂存。退出旧地址空间后加载新镜像。中途失败时回收本次已建立的资源。
6. do_wait 按指定 PID 或任意子进程匹配。活跃子进程使父进程在 WT_CHILD 睡眠，僵尸子进程可写回退出码并释放内核栈和 PCB。拒绝非法写回地址。
7. 用 hello 验证启动，testbss 验证初始化，exit 和 badarg 验证等待。记录 init check memory pass. 是否出现。
~~~~

## 提示词 3 复制用户进程

~~~~text
[PROMPT]
实现 Lab5 模块三的 do_fork、copy_mm、dup_mmap 和 copy_range。沿用 Lab4 的 PCB 分配、内核栈、PID 和调度器，补齐用户地址空间、父子关系和失败清理。普通 fork 接入 Challenge 的 COW 路径，同时保留逐页复制能力。

[RELY]
- do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf) 提供父进程现场。
- copy_thread 使用子进程自己的内核栈，预置 forkret 和子进程返回寄存器。
- mm_create、setup_pgdir、lock_mm、dup_mmap、exit_mmap、put_pgdir 与 mm_destroy 管理地址空间。
- pmm 使用 get_pte、page_insert、alloc_page、page2kva 和物理页引用计数。
- set_links 同时加入全局链表和父进程子链表，另有 hash_proc 与 wakeup_proc。

[GUARANTEE]
父进程成功返回子 PID，子进程从 a0=0 恢复。发布时，子进程已有一致的 PID、父子关系、栈和页表。分配失败时返回原错误码并回收子进程已占用的资源。

[SPECIFICATION]
前置：current、父 trapframe 和原映射有效。每个复制区间由现有 VMA 定义并按页对齐。
1. 内核线程从没有用户 mm 的父线程创建时继续使用内核页表。CLONE_VM 共享整个 mm 并递增进程引用。普通 fork 创建独立 mm 和根页表。
2. 复制 VMA 的范围和权限，遍历有效页映射。逐页复制路径分配新页并拷贝一整页；COW 路径按 Challenge spec 发布共享映射。
3. 创建子 trapframe 和 context，父子使用各自的内核栈。子进程首次 trap 返回时看到 a0=0。
4. 在关中断区间登记 PID、哈希和父子链表，然后唤醒子进程。进程切换到不同根页表时同步 satp 和 TLB。
5. 失败时回收子进程映射、页表、VMA、内核栈和 PCB。已降级为 COW 的源页继续有效，并在只剩一个引用时按需恢复可写。
6. 用 exit、forktest 和 forktree 覆盖子进程返回、批量创建、等待任意子进程和内存回收。
~~~~

## 提示词 4 退出、让出与终止

~~~~text
[PROMPT]
实现 Lab5 模块四的 do_exit、do_yield、do_kill，并联调 do_wait、trap 和时钟中断。使用户进程正常退出、主动调度、被其他进程终止和被父进程回收。

[RELY]
- current、idleproc、initproc、PROC_SLEEPING、PROC_ZOMBIE 与 need_resched 描述进程状态。
- PF_EXITING 表示退出请求，WT_CHILD 带 WT_INTERRUPTED，可被 kill 唤醒。
- release_current_mm、exit_mmap 和 mm 引用计数管理用户地址空间。
- find_proc、link_child、wakeup_proc、schedule 和关中断宏保护进程关系与状态转换。
- trap 负责在返回用户态前检查退出和重新调度资格。

[GUARANTEE]
do_exit 发布退出码和僵尸状态，父进程获得一次回收机会。do_yield 产生重新调度请求。对有效的可终止目标，do_kill 发送退出请求并确保可打断的等待者能继续走退出流程。

[SPECIFICATION]
1. idleproc、initproc 保持系统角色。当前进程退出时先切回内核页表、递减 mm 引用，只有最后一个持有者释放地址空间。
2. 关中断设置 PROC_ZOMBIE 和退出码，唤醒等待中的父进程，将当前进程的直接子进程重新挂到 initproc，再切走。僵尸进程的内核栈和 PCB 留给等待者回收。
3. do_yield 设置 need_resched 并返回 0。时钟中断若打断用户态也设置调度标志，使忙等进程可被抢占。
4. 拒绝不存在、受保护或已经僵死的目标。首次请求设置 PF_EXITING 并唤醒可打断的等待者，重复请求返回 -E_KILLED。
5. do_wait 唤醒后检查 PF_EXITING。用户进程返回前检查退出请求，无法修复的用户访问异常终止该进程。
6. 在 QEMU 运行 yield、spin、waitkill、exit、forktree，分别观察让出、抢占、终止、退出码和回收结果。
~~~~

### 联调补充提示词

~~~~text
[PROMPT]
核对 Lab5 整机编译、forktree 回收断言和评分启动行为。依次定位编译、链接、运行和测试源码中的原因。修复后重跑同一套评分。

[RELY]
- GCC 15.1 首先在 kva2page 调用报错，跳过该诊断后链接缺少 default_pmm_manager、do_yield、do_kill。
- forktree 已到达全部用户进程退出的输出，但 nr_free_pages() 不等于启动基线。
- grade.sh 曾提前运行不带 DEBUG_GRADE 的 make，日志随后持续输出时钟 tick。
- 首次完整评分 123/130，divzero 实际输出 value is 0.，评分期望 value is -1.。

[GUARANTEE]
修复与真实失败一一对应。保留现有评分断言和原有 13 个评分用例，并报告复跑结果。

[SPECIFICATION]
1. 补齐页分配器注册并恢复 kva2page 的指针类型。
2. 检查 SLOB 合并和归还策略。将合并限制在一页内，回收到整页时交还 PMM，再跑 forktree。
3. 使评分首次编译采用 DEBUG_GRADE，避免后续复用错误配置的 trap 目标文件。
4. 用直接执行 RISC-V div 指令的测试消除 C 除零的未定义行为，检查值确实等于 -1。
5. 跑普通 QEMU、专项用例和 make grade。按实测保留各自输出与覆盖范围。
~~~~

## 提示词 5 Challenge 写时复制与预加载

~~~~text
[PROMPT]
实现普通 fork 的写时复制，并增加内核与用户态测试。分析写权限和 COW 标记的关系，给出状态转换与 ELF 预加载说明。

[RELY]
- COW 可使用 Sv39 PTE 的软件保留位。PTE_W 表示当前页表可写，PTE_COW 表示此前可写、现在延迟复制。
- VMA 记录相应区间的 VM_WRITE。用户态 store page fault 提供故障地址 tval。
- page_insert 维护页引用，并在替换映射后刷新 VA 的翻译。mm_lock 保护同一地址空间的修改。
- do_wait 通过 copy_to_user 写回退出码。嵌入的用户 ELF 来自 Makefile 与 tools/user.ld。

[GUARANTEE]
父子各自拥有页表。可写页在 fork 时可暂时共享，写入后只改变写入者的地址空间。真实只读页保持其保护。退出或取消映射后，物理页引用被正确减少。

[SPECIFICATION]
1. 普通 fork 时清除双方可写映射的 PTE_W 并设置 PTE_COW。共享原本只读的页时保留权限。
2. 写缺页核对 VMA 可写、PTE 有效且允许用户访问，并检查 PTE_COW。在锁内完成引用判断与映射更新。
3. 引用大于一时分配并复制一页，重新映射写入方并减少旧页引用。只剩一个引用时原地恢复 PTE_W。
4. copy_to_user 先检查目标的 VMA 与页表，对合法 COW 页先完成写入准备，再复制用户数据。
5. 用 test_lab5_cow 验证共享、失败权限检查、分离、单引用恢复、逐页复制和空闲页。用 cow 验证实际 U 态写缺页与等待写回后的隔离。
6. 查明 ELF 在构建时被嵌入、QEMU 启动时随镜像加载、exec 时才映射到用户地址空间的三个时点。对照文件系统加载与按需调页解释实验取舍。
~~~~

## 分支任务提示词 GDB 跟踪系统调用

~~~~text
[PROMPT]
在 QEMU 上跟踪一个一定会返回的用户系统调用。记录 ecall 前、S 态入口、sret 前和 U 态恢复后的 PC、寄存器和符号，解释 QEMU 4.1.1 中对应的异常和返回代码。

[RELY]
- hello 会经 SYS_putc 输出字符，用户 ELF 位于 obj/__user_hello.out，内核符号在 bin/kernel。
- __alltraps 是内核 trap 入口，__trapret 恢复现场并执行 sret。
- QEMU 可通过本机 GDB stub 暴露来宾指令和 CSR。宿主的源码层断点需要带符号的 QEMU。
- QEMU 对照源码是 target/riscv/cpu_helper.c 与 target/riscv/op_helper.c。TCG 和软件 MMU 的关系参考 QEMU 官方开发文档。

[GUARANTEE]
客户机记录可对应到同一次调用，按观测值解释状态变化。宿主源码阅读与实际断点现场分别记录。

[SPECIFICATION]
1. 在客户机 GDB 加载内核和用户符号，用反汇编定位实际 ecall 与 sret。
2. ecall 前记录 pc、sp、a0、a1。到 __alltraps 后核对 scause、sepc、sstatus、sscratch。
3. 以 sepc 等于原 ecall 地址加 4 为条件捕获 sret，核对 a0。返回后验证 pc、sp 与用户指令。
4. 如果有带符号的 QEMU，用宿主 GDB 捕获 riscv_cpu_do_interrupt 和 helper_sret，对照虚拟 CPU 的异常号、特权级和返回地址。
5. 用 -d in_asm,op 核对这两条指令生成的 helper 调用，说明 TCG 翻译块与 Lab2 软件 MMU 地址翻译的联系，并记录遇到的符号或断点定位问题。
~~~~
