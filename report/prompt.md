# 小组实验提示词记录

本文件采用课程模板的 [PROMPT]、[RELY]、[GUARANTEE]、[SPECIFICATION] 四部分结构，以下是每轮使用的提示词：

## 提示词 1：虚拟内存页操作

~~~~markdown
[PROMPT]

任务：完成功能模块一：虚拟内存页映射维护模块，仅修改文件 kern/mm/pmm.c，使内核能够将指定物理页映射到线性地址、更新已有映射的目标页及访问权限，并安全撤销映射。映射操作必须保证页表项、物理页引用计数和 TLB 状态一致。

操作要求：
- 仅修改项目中的 kern/mm/pmm.c。保留 LAB4 function1 标记。
- 保留已经有的 get_pte()、`get_page()`、`tlb_invalidate()`、物理内存分配接口和其他源文件，不要修改 Lab2/Lab3。
- 不改动 pmm.h 的接口。必要时，可以在同一标记区域新增 static 辅助函数，但不引入与本功能无关的抽象或依赖。
输出要求：按照下列 [RELY]、`[GUARANTEE]`、`[SPECIFICATION]` 完成代码，并给我修改后的完整 kern/mm/pmm.c。说明已验证的测试用例。

[RELY]

下面的内容均来自工程文件，名称、类型与常量必须以源码为准。

kern/mm/memlayout.h 中的类型和页描述符：
typedef uintptr_t pte_t;
typedef uintptr_t pde_t;

struct Page {
    int ref;
    uint_t flags;
    unsigned int property;
    list_entry_t page_link;
    list_entry_t pra_page_link;
    uintptr_t pra_vaddr;
};
Page.ref

kern/mm/mmu.h 中的 Sv39 地址索引和页表权限位：
#define PDX1(la) ((((uintptr_t)(la)) >> PDX1SHIFT) & 0x1FF)
#define PDX0(la) ((((uintptr_t)(la)) >> PDX0SHIFT) & 0x1FF)
#define PTX(la) ((((uintptr_t)(la)) >> PTXSHIFT) & 0x1FF)
#define PTE_V     0x001
#define PTE_R     0x002
#define PTE_W     0x004
#define PTE_X     0x008
#define PTE_U     0x010
#define PGSIZE    4096

kern/mm/pmm.h 和 kern/mm/pmm.c 中可直接使用的接口：
pte_t *get_pte(pde_t *pgdir, uintptr_t la, bool create);
struct Page *get_page(pde_t *pgdir, uintptr_t la, pte_t **ptep_store);
void tlb_invalidate(pde_t *pgdir, uintptr_t la);

#define alloc_page() alloc_pages(1)
#define free_page(page) free_pages(page, 1)

static inline ppn_t page2ppn(struct Page *page);
static inline struct Page *pte2page(pte_t pte);
static inline struct Page *pde2page(pde_t pde);
static inline int page_ref(struct Page *page);
static inline void set_page_ref(struct Page *page, int val);
static inline int page_ref_inc(struct Page *page);
static inline int page_ref_dec(struct Page *page);
static inline pte_t pte_create(uintptr_t ppn, int type);
定义如下：
static inline int page_ref_inc(struct Page *page)
{
    page->ref += 1;
    return page->ref;
}

static inline int page_ref_dec(struct Page *page)
{
    page->ref -= 1;
    return page->ref;
}

static inline pte_t pte_create(uintptr_t ppn, int type)
{
    return (ppn << PTE_PPN_SHIFT) | PTE_V | type;
}
get_pte(pgdir, la, 0) 不分配页表页，缺失时返回 NULL；`get_pte(pgdir, la, 1)` 可以分配缺失的中间级页表页，内存不足时返回 NULL。

`libs/error.h` 中定义 #define E_NO_MEM 4，因此内存不足返回 -E_NO_MEM。

[SPECIFICATION] 的验证可使用源码自带的 check_pgdir()、`check_boot_pgdir()` 和 tools/test_lab4.c 中的 test_lab4_pmm_basic()。

[GUARANTEE]

必须实现、保持一致的接口：
int page_insert(pde_t *pgdir, struct Page *page, uintptr_t la, uint32_t perm);
void page_remove(pde_t *pgdir, uintptr_t la);
除上述两项外，不要新增公开接口。

[SPECIFICATION]
page_insert
Pre-Condition：
- pgdir 指向有效的 Sv39 页表根节点，`page` 指向由现有物理内存管理器管理的有效物理页描述符。
- la 指定一个线性地址；`perm` 指定目标页表项的权限标志，是否含有 PTE_V 不影响最终有效性标志的建立。
- 当前映射与物理页引用计数在调用前处于一致状态。
Post-Condition：
- 成功返回 0。`la` 所属虚拟页映射到 page 对应的物理页，页表项有效，权限更新为调用者指定的标志；对应物理页的引用计数正确，旧的 TLB 翻译不再有效。
1：目标线性页尚无有效映射：
- 应建立新的有效页表映射，目标物理页引用计数增加一次；不改变不相关地址的映射及引用计数。
2：目标线性页已经映射到同一个物理页：
- 应允许修改权限或重复建立相同映射。执行后物理页仍是原页，引用计数不变，不能错误释放该物理页；页表权限和 TLB 状态与本次调用一致。
3：目标线性页已经映射到另一物理页：
- 应更新目标映射，使其指向新物理页。新页增加一个引用，旧页减少一个引用；仅当旧页计数降为零时释放旧页。完成后不存在旧映射被 TLB 继续使用的情况。
4：建立页表的内存分配失败：
- 必须返回 -E_NO_MEM，不能安装新映射、改变目标页引用计数或破坏此前的映射。若 get_pte() 在失败前已为这次操作创建了部分中间级页表，须恢复对应页表状态并释放本次新分配但不再使用的页表页；不释放此前就存在的页表页。
Requirements：
- 区分虚拟地址、物理页号和 struct Page * 描述符，使用工程原有 Sv39 宏和辅助函数。
- 页表项必须包含有效位 PTE_V，其余权限位体现 perm；不要私自将用户传入的 PTE_W 改写为其他权限组合。
- 在成功修改目标映射后使对应 TLB 项失效；释放旧物理页之前应保证对应旧映射不再有效。
page_remove
Pre-Condition：
- pgdir 是有效的页表根节点；`la` 可以指向已映射或未映射的线性页。
Post-Condition：
- 执行后 la 所属虚拟页不再拥有有效的页表映射，不应残留可使用的旧 TLB 翻译；其他页的映射保持不变。
- 本接口不因“地址未映射”而失败，重复调用的效果与第一次删除后相同。
Case 1：目标地址有有效映射：
- 撤销该映射，将目标页表项清为无效，目标物理页的引用计数减一；若引用计数变为零，则通过现有物理页释放接口归还该页。必须进行相应的 TLB 失效处理。
Case 2：目标地址没有有效映射：
- 包括缺少中间级页表，以及目标页表项存在但无有效位。直接安全返回，不分配新页表页、不修改任何物理页引用计数、不错误释放内存。
Requirements：
- 调用页表查找接口时不得为了删除而创建新的中间级页表。
- 不必回收仍可能包含其他有效映射的中间级页表页；本任务只要求撤销目标叶子映射。
验收条件
- 新映射：`get_pte()` 返回的页表项具有 PTE_V，且 pte2page(*ptep) == page；引用计数加一。
- 同一物理页映射到两个不同线性页后引用计数增加两次，分别删除后依次减少。
- 将 A 页替换为 B 页后，A 引用计数减少、B 引用计数增加；A 只在引用为零时被释放。
- 再次映射同一物理页时引用计数和空闲页数不变，权限更新。
- 重复 page_remove() 不发生重复释放、负引用计数或新增页表分配。
- 在不同级别的页表页分配失败时，返回 -E_NO_MEM，原状态不变。
- 兼容项目已有 check_pgdir()、`check_boot_pgdir()`、`test_lab4_pmm_basic()` 的断言；
~~~~


### 模块一报告补充提示词

~~~~markdown
[PROMPT]
补齐模块一报告的核心实现、spec/prompt 设计思路、实现迭代过程、测试范围及知识点对应关系，保留负责人 2410966-王策。根据当前仓库的真实源码、Git 记录和已有验证日志撰写，以文字分析为主。

[RELY]
模块一已在提交 ed0b8ec（finish function1）中实现，当前代码位于 kern/mm/pmm.c 的 LAB4 function1 2410966 区域。原提示词已保存为提示词 1；可核对 get_pte、page_insert、page_remove、pte_create、check_pgdir、check_boot_pgdir 和 tools/test_lab4.c。report/logs 中保留了 QEMU 自检成功与评分 30/30 的实际记录，没有保存更早的模块一逐轮生成对话或失败日志。

[GUARANTEE]
保持已有运行代码和小组分工。逐项说明新建、同页重映射、异页替换、删除及部分页表分配失败的行为；实际运行结果、静态核查和设计风险需要明确区分。不得虚构此前发生的 bug、提示词迭代或故障注入结果。

[SPECIFICATION]
- 解释四部分提示词各自提供的约束，并将 spec 的前后置条件对应到 PTE、引用计数、权限、空闲页数及 TLB 同步。
- 对照代码解释新旧物理页的引用变化、pte_create 自动加入有效位、同页权限更新仍需同步 TLB，以及回收旧页前的操作顺序。
- 说明 page_remove 的不分配式查询、幂等行为及中间表回收边界。
- 根据 ed0b8ec 与已有日志记录实现、后续联调及文档核对过程；分配失败回滚只写为静态核查，不冒充动态测试通过。
- 在测试章节列明各现有测试覆盖的行为，在总结章节补充虚拟内存、共享引用、权限、TLB 和错误恢复知识点。
- 检查 Markdown 标题、空章节和文件链接；纯文档补充不重复运行已通过的构建与评分。
~~~~

## 提示词 2：进程创建及初始化

~~~~markdown
[PROMPT]

任务：完成 Lab4 功能模块二“进程创建及初始化”，在 kern/process/proc.c 中实现 alloc_proc、proc_init、kernel_thread、do_fork、copy_thread、copy_mm 六个函数，使系统能够建立 idleproc 并创建、登记 initproc，为模块三的调度准备好首次运行现场。

操作要求：
- 将实现放在 LAB4 function2 区域，保留标记并将 YOUR CODE 替换为 2412449。
- 复用已有 get_pid、hash_proc、find_proc、setup_kstack、put_kstack、forkret 等辅助函数，保持 proc.h 的公开接口不变。
- 不填写模块三，不修改模块一、Lab2/Lab3、评分脚本或已有测试断言。
- 当前只创建内核线程。不要实现用户地址空间复制、页面置换或完整的进程退出与回收。
- 按下列契约完成代码，检查实际头文件和汇编恢复链路，说明验证命令、结果与验证范围；未运行的测试不得写成通过。

[RELY]

1. 类型与字段以仓库源码为准：
- proc_struct 包含 state、pid、runs、kstack、need_resched、parent、mm、context、tf、pgdir、flags、name、list_link、hash_link。
- PROC_UNINIT、PROC_RUNNABLE 等状态在 kern/process/proc.h 中定义。
- struct context 按顺序保存 ra、sp、s0～s11。
- struct trapframe 包含 gpr、status、epc、badvaddr、cause；gpr 包含 a0、sp、s0、s1 等寄存器字段。
- MAX_PROCESS = 4096，MAX_PID = MAX_PROCESS * 2；KSTACKPAGE = 2，KSTACKSIZE = KSTACKPAGE * PGSIZE。
- CLONE_VM 定义在 kern/mm/pmm.h 中，值为 0x00000100。
- E_NO_MEM、E_NO_FREE_PROC 定义在 libs/error.h 中，分别为 4、5。

2. 现有资源与接口：
- 全局 proc_list、hash_list、idleproc、initproc、current、nr_process。
- kmalloc/kfree 分配与释放 PCB；setup_kstack/put_kstack 分配与释放新线程的内核栈。
- get_pid 分配 PID；hash_proc 将线程登记到 PID 哈希表；find_proc 查找正 PID 的线程。
- list_init 初始化链表头或结点；list_add 插入结点；wakeup_proc 要求原状态非 RUNNABLE/ZOMBIE，并将状态改为 PROC_RUNNABLE。
- boot_pgdir_pa 是内核页表根的物理地址；bootstack 是当前启动栈的起始地址。
- read_csr(sstatus)、SSTATUS_SPP、SSTATUS_SPIE、SSTATUS_SIE 以及 local_intr_save/local_intr_restore 已存在。
- 当前运行实体已经在内核态；调用创建接口时 current、传入的 trapframe 和线程函数均有效，current->mm == NULL。

3. 首次运行链路：
switch_to 恢复 context 后返回到 forkret，forkret 调用 forkrets(current->tf)；forkrets 将 sp 指向中断帧并进入 __trapret，随后恢复寄存器并执行 sret。kernel_thread_entry 从 s0 取线程函数，从 s1 取参数，调用函数后进入已有 do_exit。

[GUARANTEE]

必须实现并保持以下接口：
static struct proc_struct *alloc_proc(void);
void proc_init(void);
int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags);
int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf);
static void copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf);
static int copy_mm(uint32_t clone_flags, struct proc_struct *proc);

- 创建成功返回正 PID；进程数量达到上限返回 -E_NO_FREE_PROC；PCB 或内核栈分配失败返回 -E_NO_MEM。
- 失败不能遗留已分配 PCB/内核栈，不能把半初始化线程挂到全局链表或计入 nr_process。
- 新线程在完整初始化、分配 PID 并登记后才能变为 RUNNABLE；登记过程需要保护中断状态。
- 所有内核线程共用 boot_pgdir_pa，mm 保持 NULL；每个新线程独立拥有内核栈和栈内的 trapframe。

[SPECIFICATION]

alloc_proc
Pre-Condition：内核堆已经初始化，boot_pgdir_pa 已经就绪。
Post-Condition：分配失败返回 NULL；成功返回独立 PCB。先将字段清零，再设 state = PROC_UNINIT、pid = -1、pgdir = boot_pgdir_pa，并初始化 list_link/hash_link。runs、kstack、need_resched、parent、mm、tf、context、flags 和 name 均处于安全初始状态。

copy_mm
Pre-Condition：current 有效，新 PCB 已分配，当前实验创建的是内核线程。
Post-Condition：用断言检查 current->mm == NULL；proc->mm 保持 NULL 并返回 0。不为内核线程创建独立用户地址空间，保留 clone_flags 形参以便后续实验扩展。

copy_thread
Pre-Condition：proc 已拥有 KSTACKSIZE 大小的独立内核栈，tf 指向有效模板。
Post-Condition：在 kstack + KSTACKSIZE - sizeof(struct trapframe) 安放并复制中断帧，不保存调用者局部变量的地址；只将副本中的 a0 设为 0。esp 非零时作为恢复后的 sp，否则使用新线程中断帧地址；context.ra 指向 forkret，context.sp 指向新中断帧。原 tf 保持不变。

do_fork
Pre-Condition：current、tf 有效，进程链表与哈希表已初始化。
成功路径：检查数量上限 → alloc_proc → 设置 parent = current → setup_kstack → copy_mm → copy_thread → 关中断 → get_pid、hash_proc、插入 proc_list、nr_process 加一、wakeup_proc → 恢复原中断状态 → 返回 PID。
失败路径：
- 数量达到 MAX_PROCESS 时立即返回 -E_NO_FREE_PROC，不分配资源。
- PCB 分配失败时返回 -E_NO_MEM。
- 内核栈分配失败时只释放已分配 PCB。
- copy_mm 返回错误时按“内核栈 → PCB”的逆序回收，并原样返回错误码。
Requirements：失败路径不占用 PID、不修改全局登记结构或进程数；临界区结束后恢复进入前的中断状态。

kernel_thread
Pre-Condition：fn、current 有效。
Post-Condition：清零临时 trapframe，将 fn 放入 s0、arg 放入 s1，epc 指向 kernel_thread_entry；status 在原 sstatus 上设置 SPP/SPIE、清除 SIE，保留其他位。调用 do_fork(clone_flags | CLONE_VM, 0, &tf)，原样返回其结果。

proc_init
Pre-Condition：启动阶段只调用一次，物理页与内核堆可用，页表基址已设置。
Post-Condition：初始化 proc_list 和每个 hash_list 桶；为当前启动现场建立 idleproc，设 pid = 0、state = PROC_RUNNABLE、kstack = bootstack、need_resched = 1，命名 idle、计入 nr_process，并设 current = idleproc。idle 不通过普通 fork 创建，不加入普通进程链表与 PID 哈希表。再用 kernel_thread(init_main, "Hello world!!", 0) 创建 PID 为 1 的 initproc，经 find_proc 找回并命名 init。初始化必需资源失败时给出 panic，不能静默继续。

验收条件：
- 兼容 tools/test_lab4.c 中 test_lab4_process_create 的断言。
- 检查新 PCB 初值、独立栈、中断帧复制、a0/sp/ra、函数和参数、状态位、父子关系及 PID/链表登记。
- 注入 PCB 和内核栈分配失败，检查错误码、资源回收及全局状态不变；检查进程上限。
- 分别在中断已开启和关闭时创建线程，确认退出后保持原状态。
- 用 RISC-V 工具链编译实际 proc.c；模块三缺失时，独立测试不得冒充 QEMU 调度或完整 make grade 结果。
~~~~

### 模块二补充提示词：修复初始化时的短名称越界读取

~~~~markdown
[PROMPT]
在模块二的独立测试中，AddressSanitizer 报告 proc_init 调用 set_proc_name(idleproc, "idle") 时发生 global-buffer-overflow：原有 set_proc_name 使用 memcpy 固定读取 PROC_NAME_LEN（15）字节，而 "idle" 字符串对象只有 5 字节。请修复此处与进程初始化直接相关的辅助函数缺陷。

[RELY]
kern/process/proc.c 中 set_proc_name 已先清零整个 name 数组；name 长度为 PROC_NAME_LEN + 1。项目 libs/string.c 提供 strncpy，遇到字符串终止符后补零，最多写入指定长度；不需要改动字符串库或公开接口。

[GUARANTEE]
保留 set_proc_name 的签名、返回目标名称缓冲区的行为、最长 15 个字符的限制及最终终止符；短名称不能越界读取，长名称不能越界写入。

[SPECIFICATION]
将固定长度 memcpy 改为有长度上限的字符串复制，保留数组清零。测试空名称、"idle"、"init" 和超过 15 字符的名称；重新运行 ASan/UBSan 独立测试与 RISC-V 源码编译。报告记录真实报错和修复结果，不把独立测试写成整机调度通过。
~~~~


## 提示词 3：进程调度与上下文切换

~~~~markdown
[PROMPT]

任务：完成 Lab4 功能模块三，在已有线程创建代码上实现 cpu_idle、schedule、proc_run 和 switch_to，让 idleproc 能切换到 initproc，并支持多个内核线程主动调用 schedule 后轮流运行。按 2410668-吕昊远署名，更新报告和验证记录。

操作要求：
- 分别填写 kern/process/proc.c、kern/schedule/sched.c、kern/process/switch.S 中的 LAB4 function3 区域，保留标记并将 YOUR CODE 替换为 2410668。
- 保持模块一和模块二的功能以及现有公开接口，复用进程链表、中断保护和页表辅助函数。
- 修复阻止整机编译或运行的已知基础问题：kmalloc.c 将整数地址传给 kva2page 的类型转换、启动页表缺少低地址恒等映射、lsatp 使用 32 位分页模式及缺少 TLB 同步。
- 不扩展用户进程、抢占调度、完整 do_exit 或复杂调度策略；不修改评分断言或删除失败检查来获得通过。
- 使用 RISC-V 工具链构建，实际运行 QEMU 和原有 make grade，准确记录遇到的问题、修复与验证范围。

[RELY]

1. struct context 在 kern/process/proc.h 中按顺序保存 ra、sp、s0～s11，共 14 个 uintptr_t。当前是 RV64，REGBYTES = 8，结构大小为 112 字节；汇编偏移必须与此布局完全一致。
2. proc_list 保存普通线程的 list_link；idleproc 不在该链表内。current 是当前运行线程，PROC_RUNNABLE 同时用于就绪和正在运行的线程。
3. 模块二已经创建 PID 为 0 的 idleproc、PID 为 1 的 initproc，并设置 idleproc->need_resched = 1。新线程的 context.ra 指向 forkret，context.sp 指向自己的 trapframe。
4. list_next、le2proc、local_intr_save/local_intr_restore、switch_to 声明等均已提供。关中断操作必须保存并恢复进入前的状态，允许 schedule 与 proc_run 嵌套保护。
5. proc->pgdir 保存页表根的物理地址。当前页表模式为 Sv39，satp 的 MODE 应为 8 << 60，根物理页号为 pgdir >> RISCV_PGSHIFT，更新后需要 sfence.vma。
6. STORE/LOAD 和 REGBYTES 由 libs/riscv.h 提供；a0 和 a1 分别传入旧、新 context 指针。switch_to 是普通函数调用，按调用约定保存 ra、sp 及被调用者保存的 s0～s11。
7. tools/test_lab4.c 已提供页映射、创建和多线程主动让出 CPU 的测试；Makefile 的 grade 在临时副本中通过 inject.py 加入测试，不应往正式内核永久插入评分钩子。

[GUARANTEE]

实现并保持以下接口：
void cpu_idle(void);
void schedule(void);
void proc_run(struct proc_struct *proc);
void switch_to(struct context *from, struct context *to);

- 调度只能选取 PROC_RUNNABLE 的普通线程，无合适线程时使用 idleproc。
- 更新 current、切换 satp 和迁移执行现场处于关中断的临界区；返回原线程后恢复其进入前的中断状态。
- 同一线程继续执行时不调用 switch_to；每次调度将所选线程的 runs 加一。
- switch_to 仅保存和恢复寄存器，不分配资源，不选择线程，也不修改 PCB 管理结构。

[SPECIFICATION]

cpu_idle
Pre-Condition：proc_init 已完成，current 为有效 idleproc。
Post-Condition：永不返回，持续检查 current->need_resched；有调度请求时调用 schedule，无请求时继续空闲循环。

schedule
Pre-Condition：current、idleproc、proc_list 以及链表内各 PCB 有效。
执行步骤：保存并关闭中断 → 清除 current->need_resched → 从 current 的下一结点循环查找（当前为 idle 时从表头后开始）→ 跳过表头与非 RUNNABLE 线程 → 遍历一圈未找到时选择 idleproc → 所选线程 runs 加一 → 所选线程不同于 current 时调用 proc_run → 恢复原中断状态。
边界条件：空链表、所有普通线程阻塞、只有当前线程可运行、从链表末尾回绕、跳过睡眠与僵尸线程。不得把链表头转换成 proc_struct。

proc_run
Pre-Condition：目标线程已准备好上下文和页表，current 有效。
Post-Condition：目标等于 current 时直接返回；否则保存并关闭中断，记下 prev，设 current = proc，加载目标页表并刷新 TLB，调用 switch_to(&prev->context, &proc->context)。以后原线程再次恢复到这里时，继续恢复其保存的中断状态。

switch_to
Pre-Condition：a0 指向可写旧 context，a1 指向有效新 context，二者遵守 struct context 的布局。
Post-Condition：将 ra、sp、s0～s11 保存到 a0 指向的 0～13 个寄存器槽，再从 a1 的对应槽恢复同一组寄存器，ret 到目标 ra。不得覆盖 a0/a1 指针后再用其访问剩余字段。新线程第一次恢复时应经 forkret → forkrets → __trapret → kernel_thread_entry 进入指定函数。

验收条件：
- RISC-V 默认工具链完整构建通过。
- QEMU 普通启动输出 initproc 的 PID、名称和两条问候信息；已有 do_exit 的 process exit panic 属于框架退出行为。
- 原评分中的 PMM、Process Create、Schedule 均通过，实际记录分数。
- 为调度选择补充空链表、全部阻塞、回绕、仅当前可运行和中断状态检查；说明宿主机模拟与真实汇编切换的差别。
- 回归模块二的创建及失败清理测试；保留真实日志，不编造执行结果。
~~~~

### 模块三补充提示词：评分辅助线程的返回类型

~~~~markdown
[PROMPT]
完整内核已编译并在 QEMU 运行，第一次 make grade 在编译注入测试时失败：tools/test_lab4.c 的 test_lab4_thread 声明为 void (*)(void *)，而 kernel_thread 的入口参数类型是 int (*)(void *)。GCC 15 报告 incompatible-pointer-types。请修复此处测试辅助函数的类型不匹配，并继续实际评分。

[RELY]
该辅助线程在循环中调用 schedule，末尾为无限循环，不会正常返回；kernel_thread 的签名是已有公开接口。评分脚本的断言和输出检查应保留。

[GUARANTEE]
只将辅助线程的返回类型改为 int，使声明与创建接口一致；不改线程执行逻辑、不删除或放宽任何评分断言。同步移除独立测试中针对旧错误声明的诊断抑制。

[SPECIFICATION]
重新执行默认工具链 make grade，并回归模块二/三独立测试。只有实际输出全部 PASS 及总分后，才在报告填写评分结果。
~~~~

## Challenge 提示词：中断控制与多级页表分析

~~~~markdown
[PROMPT]

任务：完成 Lab4 指导书中的 Challenge 思考题，以 2412449-石晨昊负责的扩展分析写入 report/report.md，并在本文件记录提示词。回答三个问题：local_intr_save/local_intr_restore 如何控制中断；结合 Sv32/Sv39/Sv48 说明 get_pte 两段代码相似的原因；讨论是否拆分页表查找与分配。

操作要求：
- 阅读当前 sync.h、intr.c、pmm.c、mmu.h 和 riscv.h，以真实实现为依据。
- 用 RISC-V 官方规范核对分页参数和中断语义，附来源链接；源码分析与改进建议应明确区分。
- 保留模块一、二、三的分工；本次只补思考题及文档，不改公开接口或运行代码，不把建议接口写成已实现。
- 将回答接在实验内容的模块三之后，说明分析和验证范围，不为纯文档变更重复编译或虚构运行结果。

[RELY]

- __intr_save 检查 sstatus.SIE；原值为 1 时调用 intr_disable 并返回 1，否则返回 0。
- __intr_restore 仅在 flag 为 1 时调用 intr_enable；intr_disable/intr_enable 分别通过 clear_csr/set_csr 清除或设置 SSTATUS_SIE。
- schedule 和 proc_run 分别保存自己的中断标志，形成嵌套临界区。
- get_pte 通过 PDX1、PDX0 查找或分配两层中间页表，再用 PTX 返回末级 PTE 指针。它创建页表页，不分配最终数据页。
- 实际宏的移位为 30、21、12，索引掩码为 0x1FF；当前 RV64 的 PTE 为 8 字节。mmu.h 顶部保留了旧 Sv32 示意注释。
- page_insert 已包含 get_pte 部分分配失败时的回滚；get_page/page_remove 使用 create = 0。
- 启动页表存在高层大页叶子，而当前 get_pte 只检查 V 位并按固定层数向下遍历。

[GUARANTEE]

回答覆盖全部提问，与源码和已查证的硬件语义一致。明确区分 SIE 与 SPIE、本 hart 关中断与多核互斥、PTE 指针存在与映射有效、页表页与数据页、现有实现与建议设计。保留已有编程模块的评分记录，说明 Challenge 本身为静态分析。

[SPECIFICATION]

1. 中断控制：解释两个宏的展开与辅助函数，给出原 SIE 为 0/1 的保存、关闭、恢复结果；说明每层使用独立变量时嵌套保护的顺序，以及 flag = 0 时恢复函数不做操作的前提。说明它不保存完整 sstatus、不屏蔽同步异常，也不提供多核互斥。
2. 分页模式：用表格比较 SXLEN、有效地址位宽、每级索引、页内偏移、级数、PTE 大小、每表项数和 satp.MODE；说明 Sv39/Sv48 的高位规范地址要求。
3. 相似代码：分别对应源码中的两次中间表定位或创建，解释分配、引用计数、清零、物理页号写入和进入下一级的重复模式；在根表已提供、映射 4 KiB 页的条件下，对比三种模式所需的中间表创建次数。
4. 接口设计：先评价现有 create 参数的优点和副作用，再给出只查询、按需建立、共用内部 walker 的方案。必须说明失败时新增表页的逆序回滚、旧表页保护、叶子与大页识别，以及并发和 TLB 同步的责任；不实际重构当前接口。
5. 核对：检查 Markdown 结构、分工和链接；用源码确认关键推论，不把 30/30 解释为对这些思考题的自动评分。
~~~~

