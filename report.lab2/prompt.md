# Lab2 提示词汇总

本文件按照课程提示词模板汇总本实验的任务指令，采用 [PROMPT]、[RELY]、[GUARANTEE]、[SPECIFICATION] 四部分结构。环境路径与运行指令已统一为当前 CompilerLab 和 QEMU 4.1.1 配置。对应实现与测试结果见 [report.md](report.md)。

## 提示词 1

~~~~markdown
[PROMPT]

任务：
完成 ucore 的物理内存初始化模块。在 code/kern/mm/pmm.c 的“//lab2 function1 YOUR CODE”位置实现 static void page_init(void)。目标是把设备树报告的物理内存转化为 Page 描述符数组，保留已被系统占用的页面，并将剩余完整页面登记到当前分配器。

操作要求：
- 直接修改实际文件，主要修改范围为 code/kern/mm/pmm.c。
- 保留当前 First-Fit 管理器及已有初始化流程，不实现 Best-Fit。
- 可以增加本文件内部的 static 辅助函数，不改变公共接口。
- 不修改现有分配器和官方测试。
- 在 lab2 分支工作，保留已有改动；未经我逐次明确允许，不执行 commit，也不执行 push。
- 保留 lab2 任务标记，不猜测负责人的学号。
- 暂不修改实验报告。

输出要求：
说明实际改动，并用“固件 → 内核 → 描述符数组 → 空闲页”解释最终布局。列出执行过的检查和真实结果；不能运行的检查要说明具体阻塞，不得声称已经通过。


[RELY]

以下是当前框架的相关接口和约定，请先阅读对应代码核对。

1. 初始化调用关系

kern_init() 已先执行 dtb_init()，随后调用 pmm_init()。
pmm_init() 的顺序是：

init_pmm_manager();
page_init();
check_alloc_page();

进入 kern_init() 前，启动汇编已经开启 Sv39 分页。page_init() 不需要再次建立或启用页表。

2. 设备树查询接口，位于 code/kern/driver/dtb.h

uint64_t get_memory_base(void);
uint64_t get_memory_size(void);

两者返回物理内存起始地址和字节长度。当前框架采用从 DRAM_BASE 开始的一段连续物理内存模型。

3. 地址与页面定义

code/libs/riscv.h：
#define DRAM_BASE 0x80000000

code/kern/mm/mmu.h：
#define PGSIZE  4096
#define PGSHIFT 12
#define PPN(la) (((uintptr_t)(la)) >> PGSHIFT)

code/kern/mm/memlayout.h：
#define PHYSICAL_MEMORY_END    0x88000000
#define PHYSICAL_MEMORY_OFFSET 0xFFFFFFFF40000000
#define KERNEL_BEGIN_PADDR     0x80200000
#define KERNEL_BEGIN_VADDR     0xFFFFFFFFC0200000
#define KERNBASE               0xFFFFFFFFC0200000

libs/defs.h 已提供 ROUNDUP 和 ROUNDDOWN。实际写代码时使用工程宏，不再定义另一套地址常量。

4. 页描述符，位于 code/kern/mm/memlayout.h

struct Page {
    int ref;
    uint64_t flags;
    unsigned int property;
    list_entry_t page_link;
};

工程提供 SetPageReserved、PageReserved 等状态宏。

5. pmm.c 已定义的全局状态

struct Page *pages;
size_t npage = 0;
uint64_t va_pa_offset;
const size_t nbase = DRAM_BASE / PGSIZE;

含义：
- npage 是可管理物理页号的排他上界。
- 描述符数量是 npage - nbase。
- pages[i] 描述物理页号 nbase + i。

6. 可用接口和链接符号

extern char end[];
static void init_memmap(struct Page *base, size_t n);

end 表示内核镜像结束位置的虚拟地址。init_memmap() 将页面区间交给当前管理器；现有 First-Fit 要求这些页面在登记前处于 Reserved 状态。

pmm.h 已提供：
- PADDR(kva)：从内核虚拟地址转换为物理地址，转换依赖 va_pa_offset。
- pa2page(pa)：取得对应描述符，依赖 pages 和 npage。其实现只检查页号上界，调用方还需保证下界有效。
- page2pa(page)：取得描述符对应的物理页起始地址。

还可以使用 cprintf()、panic()。


[GUARANTEE]

必须实现：

static void page_init(void);

成功返回后：
- va_pa_offset、npage、pages 已正确建立；
- 已输出探测到的物理内存范围；
- 固件、内核和描述符数组占用的页面保持保留；
- 剩余完整页面已交给当前管理器，且只登记一次。

不修改 init_pmm_manager() 的管理器选择。


[SPECIFICATION]

## page_init

Pre-Condition：
- 启动映射已经建立。
- 设备树解析已经执行。
- 当前物理内存管理器已经完成自身初始化，尚未登记物理内存空闲区。

Post-Condition：
- 地址转换偏移与启动映射一致，在首次依赖该偏移前已设置。
- 可管理范围同时满足设备树报告范围和 PHYSICAL_MEMORY_END 的上界约束。
- 内存末端不足一页的部分不计入可管理页。
- pages 位于 end 之后，按页对齐；数组具有 npage - nbase 个描述符，并且整体位于合法内存中。
- 描述符先全部建立保留状态，再将合法空闲区交给分配器。
- 固件、内核、描述符数组及它们涉及的对齐占用，均不属于空闲区。
- 空闲区按整页计算，登记页数与区间长度一致。

Case 1：内存信息有效，且布局后仍有完整空闲页。
完成描述符初始化，登记描述符数组之后的完整可用页面。返回后，这些页面可以被现有 First-Fit 正常分配。

Case 2：内存信息无效，或无法容纳内核与完整描述符数组。
对零长度、地址范围溢出、不支持的内存起点、内核或数组超出可管理范围等情况，使用 panic 终止初始化。在确认数组能够容纳之前，不向数组范围写入数据。

Case 3：内核和描述符数组能够容纳，但之后没有完整空闲页。
保留正确的描述符布局，不调用 init_memmap()；分配器的空闲页数保持为零。

Requirements：
- 区分物理地址、虚拟地址、物理页号和描述符地址。
- 范围相加、描述符大小计算、向上对齐不得发生整数溢出。
- 不把设备树报告的总页数直接赋给 npage。
- 不假设描述符数组不占物理内存。
- 不引入宿主系统内存分配函数或额外动态分配器。

验证：
1. 根据代码给出默认 128 MiB 内存下的布局计算关系，检查描述符数量、首尾边界和空闲页数。
2. 检查物理页与描述符之间的转换是否符合现有接口。
3. 检查无效内存、描述符空间不足和没有完整空闲页三类边界。
4. 尝试实际编译；当前 Best-Fit 框架还缺少管理函数和内部状态，可能使整个工程无法编译。若因此受阻，记录具体错误，不通过修改 Best-Fit、删除源码或改动测试来绕过。完整启动和评分将在 Best-Fit 实现后联合验证。
~~~~

## 提示词 2

~~~~markdown
[PROMPT]

任务：
在 code/kern/mm/best_fit_pmm.c 实现 Best-Fit 物理页分配器，并在 code/kern/mm/pmm.c 中启用它。

先阅读当前项目的 code/kern/mm/default_pmm.c：理解 First-Fit 如何维护空闲块链表、初始化页面、分配连续页面、释放页面并合并相邻空闲块。然后参照相同的框架约定实现 Best-Fit。两种算法的关键区别是：分配 n 页时，选择所有足以满足请求的空闲块中最小的那一块；分配后若块有剩余，保留剩余页作为一个有效空闲块。这是一个完整的分配器模块任务，包含分配器内部状态、管理器接口实现、测试接入点和管理器选择，不是只补一个函数。

操作要求：
- 直接修改实际文件，不能只输出代码片段。
- 核心修改范围为 code/kern/mm/best_fit_pmm.c 和code/kern/mm/pmm.c；若确有编译所需修改，说明原因并保持范围最小。
- 不改 best_fit_pmm.c 中已有的 basic_check() 和 best_fit_check()，不改 tools/ 下的测试及注入脚本。
- 在 pmm.c 中将 init_pmm_manager() 使用的管理器切换为best_fit_pmm_manager；保留 page_init() 和调用顺序。
- 保留现有改动；不切换分支。未经我逐次明确允许，不执行 commit、amend 或 push。
- 暂不修改实验报告。


[RELY]

请以当前仓库文件为准，以下是实现所需的接口和约定。

1. pmm_manager，定义在 code/kern/mm/pmm.h：

struct pmm_manager {
    const char *name;
    void (*init)(void);
    void (*init_memmap)(struct Page *base, size_t n);
    struct Page *(*alloc_pages)(size_t n);
    void (*free_pages)(struct Page *base, size_t n);
    size_t (*nr_free_pages)(void);
    void (*check)(void);
};

Best-Fit 模块应提供与这些成员匹配的私有函数，并导出 `const struct pmm_manager best_fit_pmm_manager`，声明已在 best_fit_pmm.h 中。

2. 页面描述符，定义在 code/kern/mm/memlayout.h：

struct Page {
    int ref;
    uint64_t flags;
    unsigned int property;
    list_entry_t page_link;
};

当前标志接口包括 PageReserved、SetPageReserved、ClearPageReserved、PageProperty、SetPageProperty 和 ClearPageProperty。

3. 链表接口在 code/libs/list.h。First-Fit 使用 list_entry_t 和 page_link 维护空闲块的双向链表；新增链表操作应使用项目已有接口。

4. 分配器测试位于 best_fit_pmm.c，并直接访问文件作用域名称：
- `free_list`：空闲块链表头。
- `nr_free`：当前空闲页总数。

请在该文件中以兼容方式定义这两个名称，保持测试代码不变。测试会检查链表每个块头的 PageProperty 状态和 property 块长度，并验证所有块的页数总和等于 nr_free_pages()。

5. 测试对行为的要求：
- 分配的页面互不相同，引用计数为 0，物理地址在有效范围内。
- 空闲块不足时分配返回 NULL。
- 释放连续页面后，空闲页计数正确。
- 切分块后，余下块仍能被分配。
- 最佳适配确实选择最小的可满足空闲块。
- 回收和相邻空闲块合并后，较大的连续请求可以成功。
- 测试过程中会临时保存、清空并恢复 free_list 和 nr_free，所以状态布局必须能配合现有测试操作。

6. `default_pmm.c` 可作为 Page 状态清理、链表插入和释放合并的工程内参考，但选择策略必须实现 Best-Fit，不能复制 First-Fit 后仅改管理器名称。

7. pmm.c 的 init_pmm_manager() 当前选择 default_pmm_manager；best_fit_pmm.h 已声明 best_fit_pmm_manager。
pmm_init() 会先初始化管理器，再调用 page_init() 登记空闲区，最后调用 pmm_manager->check()。


[GUARANTEE]

必须在 best_fit_pmm.c 实现并注册：

- 初始化管理器内部空闲链表和空闲页计数的函数。
- 初始化空闲物理页区间的函数。
- Best-Fit 连续页分配函数。
- 连续页释放与相邻空闲块合并函数。
- 查询空闲页数的函数。
- 调用框架提供的 Best-Fit 检查函数的管理器 check 接口。
- `const struct pmm_manager best_fit_pmm_manager`。

必须在 pmm.c 中把当前管理器切换为
`&best_fit_pmm_manager`。

允许增加 best_fit_pmm.c 内部的 static 辅助函数和状态；保持管理器公共接口不变。


[SPECIFICATION]

## 分配器初始化

Pre-Condition：
管理器开始初始化；没有页面区间登记。

Post-Condition：
空闲链表为空，空闲页数为零。

## init_memmap

Pre-Condition：
输入区间长度大于零；区间中的页面处于 Reserved 状态；输入区间由 page_init() 提供，彼此不重叠。

Post-Condition：
输入区间中的页面转为空闲状态；首页面标记为有效空闲块头，property 等于区间页数；链表记录这个空闲块；空闲页总数增加对应页数。

## alloc_pages

Pre-Condition：
请求页数大于零。

Post-Condition：
成功时返回连续的 n 个页面，分配页面不再作为空闲块出现，空闲页计数减少 n；若选中块比请求更大，剩余页面作为新的空闲块正确保留。失败时返回 NULL，空闲链表和页数不变。

Case 1：存在多个大小不小于 n 的空闲块。
选择大小最接近 n 的块；从该块起始位置分配 n 页，并正确登记可能产生的剩余块。若大小相同，允许稳定且确定的任一选择规则。

Case 2：没有大小不小于 n 的空闲块。
返回 NULL，不改变分配器状态。

## free_pages

Pre-Condition：
输入是一个由分配器分配、尚未释放、长度大于零的连续页面区间；区间不能与现有空闲区重叠。

Post-Condition：
释放页面转为空闲状态，引用计数归零，空闲页数增加 n；链表按物理页顺序维护相邻区间合并后的空闲块，块头 property 与块长度一致，非块头不保留过期块头状态。

Case 1：释放区间紧邻前一个空闲块。
将释放区间与前块合并为一个空闲块。

Case 2：释放区间紧邻后一个空闲块。
将释放区间与后块合并为一个空闲块。

Case 3：释放区间同时紧邻前后空闲块。
合并成一个覆盖三段的空闲块，链表中只保留一个有效块头。

Case 4：释放区间不与任何空闲块相邻。
将其作为新的独立空闲块插入正确位置。

## 空闲页查询与检查

nr_free_pages 返回当前空闲页总数。管理器的 check 接口调用现有 best_fit_check()。不得修改、删除、跳过该测试，也不得伪造其输出。

## 管理器接入

pmm_init() 初始化 Best-Fit 管理器后，沿用现有 page_init() 登记初始空闲页，再调用管理器检查。启动日志应能确认当前使用的管理器是 Best-Fit。

Requirements：
- 保持 `free_list` 和 `nr_free` 与现有测试的直接访问方式兼容。
- 块切分、插入、合并时保持 property 和 PageProperty 一致。
- Best-Fit 搜索不得退化为找到第一个适配块就立即分配。
- 正常测试约定下保证空闲页总数等于所有空闲块大小之和。
- 不改官方测试行为来迎合实现。


[验证]

1. 从 code 目录执行项目可用的编译及 make grade；不在源码树直接执行 tools/inject.py，make grade 会复制到临时目录注入测试。
2. 检查日志确认 page-init 和 Best-Fit 测试均通过，管理器名称显示 Best-Fit。
3. 若编译、QEMU 或 grade 因本机缺少工具而无法运行，报告实际错误，不将未运行写成通过。
4. 检查 pmm.c 的 page_init() 改动仍在，并汇报它是否通过验证。
~~~~

## 提示词 3

~~~~markdown
[PROMPT]

任务：
为当前 ucore 内核新增 Buddy System 物理页分配模块，接入现有 pmm_manager 框架，并支持通过编译选项选择运行。

新增：
- code/kern/mm/buddy_pmm.c：Buddy 分配器实现。
- code/kern/mm/buddy_pmm.h：管理器声明。

修改：
- code/kern/mm/pmm.c：根据编译选项选择 Best-Fit 或 Buddy。
- code/Makefile：提供 PMM=best_fit 和 PMM=buddy 两种构建配置，默认 best_fit。

本轮完成分配器及接入，只进行编译、启动和必要的基本检查；完整的 Buddy 行为测试将在下一轮单独设计。

操作要求：
- 直接修改项目实际文件。
- 保留已完成的 page_init()、Best-Fit 和现有启动修复。
- 不修改 First-Fit、Best-Fit 的算法和测试；不修改 tools/ 下的官方测试、注入及评分脚本。
- 不修改 struct Page 的公共布局或 pmm_manager 的接口。
- 在 lab2 分支工作，保留所有已有改动。
- 未经我逐次明确允许，不执行 commit、amend 或 push。
- 暂不编写实验报告，不实现 SLUB。

输出要求：
完成后说明：
1. 空闲块和已分配块如何记录，字段含义是什么。
2. order 的定义、对齐原点和伙伴关系。
3. 非 2 的幂请求的占用与释放契约。
4. 两种管理器的实际构建、启动命令及结果。
5. 下一轮测试可以使用的接口及内部不变量。

区分启动成功、基本检查通过与完整算法验证通过。


[RELY]

以下是当前项目已经存在的接口和状态；开始前核对实际文件。

1. 物理页管理接口，位于 code/kern/mm/pmm.h：

struct pmm_manager {
    const char *name;
    void (*init)(void);
    void (*init_memmap)(struct Page *base, size_t n);
    struct Page *(*alloc_pages)(size_t n);
    void (*free_pages)(struct Page *base, size_t n);
    size_t (*nr_free_pages)(void);
    void (*check)(void);
};

外部调用：
struct Page *alloc_pages(size_t n);
void free_pages(struct Page *base, size_t n);
size_t nr_free_pages(void);

#define alloc_page() alloc_pages(1)
#define free_page(page) free_pages(page, 1)

2. 页面结构，位于 code/kern/mm/memlayout.h：

struct Page {
    int ref;
    uint64_t flags;
    unsigned int property;
    list_entry_t page_link;
};

已有 PageReserved、SetPageReserved、ClearPageReserved、PageProperty、SetPageProperty、ClearPageProperty、set_page_ref() 和双向链表接口。允许在 Buddy 模块中定义自己的私有元数据和字段使用约定，但不得破坏其他管理器，也不能依赖尚未存在的动态内存分配器。

3. 地址及描述符关系：

PGSIZE = 4096
DRAM_BASE = 0x80000000
PHYSICAL_MEMORY_END = 0x88000000

extern struct Page *pages;
extern size_t npage;
extern const size_t nbase;

pages[i] 对应物理页号 nbase + i。
page2ppn()、page2pa()、pa2page() 已由 pmm.h 提供。

当前最多管理 32768 个物理页描述符，实际可用页面更少，因为要排除固件、内核和描述符数组。

4. 当前启动顺序：

init_pmm_manager()
    → 所选管理器 init()
page_init()
    → 所选管理器 init_memmap(base, n)
check_alloc_page()
    → 所选管理器 check()

page_init() 提供合法的连续空闲区，传入页面最初都处于 Reserved 状态。这个区间的起点不保证满足高阶 Buddy 块对齐，长度也不保证是 2 的幂。

5. Best-Fit 已完成，pmm.c 当前选择 &best_fit_pmm_manager。其原有测试不适用于 Buddy 的取整契约，不能直接复用。

6. 当前 Makefile 的 qemu、debug、test 目标使用 -kernel 加载。entry.S 已补充开启分页后跳转到高地址前所需的低地址映射，保留这些修复。

7. 官方注入测试要求当前管理器是 Best-Fit。因此默认模式必须继续使用 Best-Fit，Buddy 使用独立构建配置。


[GUARANTEE]

新增并实现以下 Buddy 私有函数：

static void buddy_init(void);
static void buddy_init_memmap(struct Page *base, size_t n);
static struct Page *buddy_alloc_pages(size_t n);
static void buddy_free_pages(struct Page *base, size_t n);
static size_t buddy_nr_free_pages(void);
static void buddy_check(void);

导出并在 buddy_pmm.h 声明：

extern const struct pmm_manager buddy_pmm_manager;

可增加模块内部 static 辅助函数和元数据。

构建行为：
- PMM 未指定或 PMM=best_fit：选择 Best-Fit。
- PMM=buddy：选择 Buddy。
- 不支持的 PMM 值应明确报错。
- 切换配置时必须保证旧目标文件不会使选择失效：可以实现可靠的配置依赖，也可以明确要求切换前 make clean。不需要为此重构整个构建系统。


[SPECIFICATION]

## 全局分配契约

order=k 表示包含 2^k 个物理页的块。

对于大小为 B=2^k 页的块，其起点物理页号相对 nbase 的偏移必须是 B 的整数倍。伙伴必须具有相同 order，并属于同一个对齐的 2B 页父块；相邻不代表互为伙伴。

申请 n>0 页时，实际占用最小的、满足 B>=n 的 2 的幂页数 B。例如申请 3 页占用 4 页，申请 5 页占用 8 页。

释放契约：
调用者传入原始返回指针和原请求数 n。free_pages(p, 3) 应归还该次申请实际占用的完整 4 页。本轮不支持部分释放一个 Buddy 分配块。

nr_free_pages() 统计真实空闲页数：
申请 3 页成功后减少 4，释放后增加 4。

非法请求 n=0 使用断言拒绝。其他释放请求以前置条件为约束；不得承诺尚未实现的任意非法指针、重复释放或错误长度检测能力。

## buddy_init

Pre-Condition：
Buddy 管理器尚未登记可用内存。

Post-Condition：
各阶空闲结构为空，空闲页数为零；私有元数据处于可接收初始空闲区的状态。

## buddy_init_memmap

Pre-Condition：
n>0，输入区间合法、不与已登记区间重叠，所有输入页面处于 Reserved 状态。

Post-Condition：
输入区间被完整划分为对齐、互不重叠的 2 的幂空闲块。不能为了方便只取最大的一块而丢弃剩余页面。所有块都位于输入区间内，不包括前后的保留页。新增空闲页数恰好为 n，注册页面引用计数为零。

Requirements：
正确处理起点不满足高阶对齐、长度不是 2 的幂、以及单页区间。元数据容量和最大 order 必须覆盖当前框架支持的内存范围，不得越界。

## buddy_alloc_pages

Pre-Condition：
n>0，管理器已初始化。

Post-Condition：
成功时返回一个实际大小为 B 页的连续块，满足全局契约；返回块与其他分配块、空闲块及保留区均不重叠，引用计数为零，空闲页数减少 B。失败时返回 NULL，所有空闲结构、元数据和计数保持不变。

Case 1：存在目标 order 的空闲块。
移出一个该阶块并登记其分配状态。

Case 2：目标阶没有空闲块，但更高阶存在。
分裂较大的块，直到得到目标阶块；未分配的分裂伙伴进入对应空闲结构。

Case 3：不存在足够大的块。
即使总空闲页数足够，也应返回 NULL，不能把不连续页面拼成一次连续分配。

Case 4：请求或向上取整结果超出支持范围。
返回 NULL，不发生整数溢出、非法移位或状态变化。

## buddy_free_pages

Pre-Condition：
base 是仍未释放的 Buddy 分配块起点；n 是该次分配的原始请求页数。

Post-Condition：
整个实际分配块重新变为空闲；空闲页数只增加该实际块的页数，不能在每次合并时重复增加。所有空闲链表和私有元数据保持一致。

Case 1：同阶伙伴空闲，且确实属于同一个父块。
合并后继续检查更高阶伙伴，直到无法继续合并。

Case 2：伙伴已分配、保留、越界或阶数不同。
停止合并，将当前块作为该阶空闲块保留。

Requirements：
不得合并仅仅物理相邻、但不满足伙伴关系的块。不得跨入固件、内核或描述符数组区域。释放时不依赖调用者把 n 改写为取整后的大小。

## buddy_nr_free_pages

Pre-Condition：
管理器已初始化。

Post-Condition：
返回真实空闲页总数，不修改状态。

## buddy_check

Pre-Condition：
管理器已完成初始空闲区登记。

Post-Condition：
只进行本阶段必要的结构一致性检查：块大小、对齐、范围、块头状态，以及各阶页数之和与空闲页计数相符。检查不改变可分配资源。

该检查不能是无条件输出成功的空函数。完整的分配、释放、碎片和边界行为测试留到下一轮；本轮不要把结构检查描述成完整算法验证。

## 管理器选择

在 init_pmm_manager() 中按编译配置选择管理器，保持 init → page_init → check 的顺序。

Best-Fit 默认配置下，现有功能及原测试保持可运行。Buddy 配置下，启动日志必须显示 buddy_pmm_manager，并实际调用 Buddy 的接口。


[验证与交付，属于本轮操作要求]

- 在 WSL 中分别清理、编译、启动默认模式与 Buddy 模式。
- 确认两种模式日志显示各自的管理器名称。
- 默认模式继续运行现有 Best-Fit 检查。
- Buddy 模式完成初始化和结构检查。
- 若进行申请 3 页再释放的基本检查，应验证空闲页数先减少 4 再恢复，并清理临时验证改动。
- 记录实际命令、结果及失败原因，不伪造 PASS。
- 不为运行 Buddy 而修改官方 Best-Fit 测试。
- 使用当前 QEMU 4.1.1 直接执行原有 make grade；若启动或评分失败，记录具体原因，不修改官方评分逻辑。
~~~~

## 提示词 4

~~~~markdown
[PROMPT]

任务：
为已实现的 Buddy 物理页分配器设计并实现独立测试，验证初始化、分裂、取整、伙伴合并、碎片失败和资源恢复。本轮重点是测试设计与实现，不重写分配器。测试通过公开管理器接口操作分配器，期望结果依据下述契约，不能复制被测代码的计算方法来生成“正确答案”。

建议新增：
- code/kern/mm/buddy_test.c
- code/kern/mm/buddy_test.h

允许对 code/Makefile 和 code/kern/mm/pmm.c 做最小测试接入。若采用其他文件组织，先说明其目的。

操作要求：
- 直接修改实际文件。
- 默认构建不运行专项测试。
- 提供 PMM=buddy BUDDY_TEST=1 测试配置；对非 Buddy 模式启用 BUDDY_TEST=1 明确报错。
- 测试配置必须与普通 Buddy 配置隔离对象文件，并纳入共享镜像的配置切换判断。
- 保留 Best-Fit、现有启动修复和官方测试。
- 不修改 tools/ 下的官方评分和注入逻辑。
- 原则上不改 buddy_pmm.c 的算法；若测试发现缺陷，先给出可复现失败，再做必要的最小修复并记录。
- 在 lab2 分支工作，未经我逐次明确允许，不执行 commit、amend 或 push。
- 暂不编写实验报告正文。

输出要求：
交付测试代码、可重复执行命令、逐项测试结果，以及“测试场景—断言—能识别的错误”对应说明。不得把结构检查通过当作完整行为测试通过。


[RELY]

1. 公开接口

code/kern/mm/buddy_pmm.h：
extern const struct pmm_manager buddy_pmm_manager;

code/kern/mm/pmm.h：
struct pmm_manager {
    const char *name;
    void (*init)(void);
    void (*init_memmap)(struct Page *base, size_t n);
    struct Page *(*alloc_pages)(size_t n);
    void (*free_pages)(struct Page *base, size_t n);
    size_t (*nr_free_pages)(void);
    void (*check)(void);
};

struct Page *alloc_pages(size_t n);
void free_pages(struct Page *base, size_t n);
size_t nr_free_pages(void);

2. 当前 Buddy 契约

- order=k 的块包含 2^k 页。
- 对齐原点为 pages[0]，即物理页号 nbase。
- n>0 的请求占用最小的、大小不小于 n 的 2 的幂页数 B。
- free_pages(p, n) 传原指针和原请求 n，归还完整 B 页。
- 不支持部分释放。
- 请求失败返回 NULL，状态不变。
- n=0 由断言拒绝。
- 不承诺检测所有非法指针、重复释放和错误释放长度。

3. 当前私有状态

buddy_pmm.c：
- static list_entry_t buddy_free_lists[16]
- static size_t nr_free

空闲块头 PageProperty 为真，property 表示 order；已分配块不保留 PageProperty。这些私有状态不应暴露成新的生产公共接口。

4. 全局描述符关系

extern struct Page *pages;
extern size_t npage;
extern const size_t nbase;

pages[i] 对应物理页号 nbase+i。描述符有效数量为 npage-nbase。

struct Page {
    int ref;
    uint64_t flags;
    unsigned int property;
    list_entry_t page_link;
};

现有 Page 状态宏、page2pa()、page_ref()、set_page_ref() 和 PHYSICAL_MEMORY_OFFSET 可以使用。

5. 当前启动流程

init_pmm_manager();
page_init();
check_alloc_page();

当前 Buddy check 只做结构检查。Best-Fit 默认构建的算法与测试保持原样。

6. 构建

Makefile 已支持 PMM=best_fit / PMM=buddy，使用 obj/$(PMM) 隔离对象，并用 bin/.pmm_config 判断共享镜像是否需要强制重链。新增测试配置后，配置身份必须包含测试开关。不能只增加宏而继续复用普通 Buddy 对象或旧镜像。QEMU 运行目标使用 -kernel。构建环境为 WSL 的 CompilerLab，交叉工具链位于 /opt/riscv-elf-toolchains/bin，QEMU 4.1.1 位于 /home/compiler/.local/opt/qemu-4.1.1/bin；source ~/.config/compilerlab/riscv-env.sh 后执行 export QEMU=/home/compiler/.local/opt/qemu-4.1.1/bin/qemu-system-riscv64。


[GUARANTEE]

新增：

void buddy_run_fixture_tests(void);
void buddy_run_live_tests(void);

允许增加测试文件内部的 static 辅助函数和静态数据。

buddy_run_fixture_tests：
在管理器已初始化、真实页面尚未登记时运行，使用受控描述符区间验证分配器行为；结束后恢复全局状态，并让管理器回到空状态，随后正常执行真实 page_init()。

buddy_run_live_tests：
在真实 page_init() 和原管理器检查完成后运行，验证真实页面的分配、读写、释放及资源恢复。

两者只在明确启用的 Buddy 测试配置中调用。


[SPECIFICATION]

## 测试夹具与状态隔离

Pre-Condition：
当前选择 Buddy；管理器尚未登记真实空闲内存。

Post-Condition：
所有受控测试通过后，恢复原 pages、npage 等被临时替换的状态，重新初始化空管理器，确保真实 page_init() 可以正常运行。

Requirements：
- 可以使用至少 128 个静态 Page 描述符构造模拟管理区，临时设置 pages 和 npage，保持 nbase 不变。
- 通过 buddy_pmm_manager 的回调操作真实分配器代码。
- 每个测试重新初始化夹具和页面状态，彼此独立。
- 模拟描述符不代表真实可分配 RAM，禁止根据其 page2pa() 地址读写物理页内容。
- 不在真实内存已经登记后重置管理器。
- 不通过浅拷贝链表头来保存或恢复分配器。
- 大型指针表、位图和夹具放静态存储，避免耗尽内核栈。
- 只在返回指针属于夹具数组的合法范围后才做索引运算。


## 初始化区间完整覆盖

Pre-Condition：
夹具描述符全部标记 Reserved，管理器为空。

Post-Condition：
登记区间里的每个页面恰好可分配一次；区间外的页面不可分配，且保留状态不变。

Case 1：登记 [0,128)。
连续申请单页直到耗尽，恰好得到 128 个互不重复的页面；再次申请返回 NULL；全部释放后恢复 128 页。

Case 2：登记偏移 [3,120)，共 117 页。
单页耗尽结果恰好覆盖这些偏移，没有遗漏、重复和越界；不允许硬编码启动时空闲页数来判断。

Case 3：只登记偏移 7 的单页。
首次申请成功，再次申请失败，释放后可以重新申请。

说明：本阶段针对单次区间登记，不额外要求相邻的多次 init_memmap 调用自动合并，避免扩展上一轮契约。


## 分配取整与失败不变性

Pre-Condition：
使用资源足够的独立夹具。

Post-Condition：
每次申请的计数变化、返回块对齐和释放恢复符合契约。

Case 1：
覆盖请求 1、2、3、4、5、7、8、9，其实际占用分别为 1、2、4、4、8、8、8、16 页。使用明确的期望值，不调用被测取整辅助函数计算答案。

Case 2：
连续保留多个不同大小的分配块。根据各自实际占用检查它们互不重叠；释放使用各自原始请求数，最终计数恢复。

Case 3：
申请大于可用总量的请求、32769 和 (size_t)-1，均应返回 NULL。除了检查计数不变，还要验证失败之后原有可用页面仍可正常分配，避免只检查计数遗漏链表损坏。

Requirements：
不要求非法释放被识别。n=0 会触发 panic，不能放在正常成功路径中执行；若验证该断言，使用单独运行配置，并把它作为预期失败报告。


## 分裂与跨阶合并

Pre-Condition：
使用完整对齐的 [0,128) 夹具。

Post-Condition：
小块分配能触发大块分裂；全部释放后，重新申请 128 页成功。

Case 1：
分配不同阶的块并保留，随后以与分配不同的顺序释放，验证最终能恢复整个高阶块。

Case 2：
将 128 页全部分配为单页，再按固定乱序释放。最后申请 128 页应成功，检测递归合并链是否完整。

Requirements：
断言基于返回页面实际偏移，不依赖空闲链表的遍历顺序。每次计数变化必须准确，合并不能重复增加空闲页数。


## 相邻页面不一定是伙伴

Pre-Condition：
登记 [0,4)，将四页全部分配，按实际返回地址建立“偏移到指针”的映射。

Post-Condition：
只有满足父块对齐关系的页面能够合并。

Case 1：
仅释放偏移 1 和 2。此时空闲页总数为 2，但申请 2 页必须失败；失败后两个单页仍然可分配。

Case 2：
在独立重建的夹具中，仅释放偏移 0 和 1。申请 2 页应成功，返回块起点为偏移 0。

这组对照必须存在，用于区分“相邻合并”和真正的伙伴合并。


## 碎片导致的分配失败

Pre-Condition：
将 [0,128) 全部分配为单页。

Post-Condition：
空闲总量足够不等于存在可分配的高阶块。

Case：
仅释放所有偶数偏移页面，共 64 页；申请 2 页应失败且不改变状态。随后释放仍持有的奇数页面，重新申请 128 页应成功。


## 真实内存集成检查

Pre-Condition：
真实 page_init() 完成，Buddy 已登记真实空闲页；原有结构检查通过。

Post-Condition：
测试后空闲页数恢复，所有测试分配都已释放，没有修改固件、内核或描述符数组。

Case：
记录本次运行的初始空闲页数。分配原请求为 3、5、1 页的多个块，计数分别按 4、8、1 页变化；核实块不重叠、页地址与阶对齐正确、范围合法。

通过 page2pa() 和当前虚实偏移访问实际分配内存，向每个实际块写入不同的确定性内容，再读取校验，确认不同分配之间不会相互覆盖。释放时使用原请求数，最终页数恢复。对释放后再次分配的内存先写后读，不假设分配器自动清零。

Requirements：
不要假定测试前后内核构建的空闲页数固定；新增测试代码和 BSS 可能改变内核结束地址。


## 测试可信性与自动执行

- 每项输出独立 PASS；断言失败时不能继续输出最终成功标记。
- 最终成功标记只在全部测试与清理完成后输出。
- 提供有界的自动运行方式，保存或展示真实日志。
- 内核本来会进入无限循环；超时终止只有在日志出现完整成功标记、且没有 panic/FAIL 时才可以作为预期结束。
- 不把 QEMU 进程被终止本身当作测试成功。


## 配置回归

验证：
普通 Best-Fit → 普通 Buddy → Buddy 测试 → 普通 Buddy → Best-Fit。

不 clean 切换也必须运行正确配置；普通 Buddy 模式不得残留专项测试执行。默认模式原有 Best-Fit 检查继续通过。在当前 CompilerLab 中使用 QEMU 4.1.1 直接运行原有 make grade，并记录实际命令和结果。


[本轮交付说明]

除代码和运行结果外，给出：
- 各测试为什么能发现对应错误。
- 测试尚未覆盖的内容。
- 若有算法修复，给出修复前失败与修复后通过的证据。

如环境允许，在临时副本做两项测试有效性检查：
1. 将非整幂请求的扣减错误改成按 n 扣减。
2. 禁止释放时的伙伴合并。

对应测试应失败；临时错误不能进入最终代码。如果没有执行，明确写未执行，不虚构结果。
~~~~

##### 追加提示词 - 1

~~~~markdown
我发现个事，bt_case_adjacent_not_buddy() 和 bt_case_buddy_pair() 先申请 4 页，再逐页释放，这违反了“不支持部分释放”的约定。我认为应改成申请四次单页，按实际返回地址建立偏移映射，然后再分别释放偏移 1、2 或 0、1，完成对应检查并归还剩余分配。这里无需修改 Buddy 算法，但是这两项测试目前不能作为符合契约的验证证据。
~~~~

## 提示词 5

~~~~markdown
[PROMPT]

任务：
在现有 Buddy 页分配器之上实现教学用的简化 SLUB 对象分配模块，提供任意请求大小的 kmalloc(size) / kfree(ptr) 接口，并接入内核初始化和可选构建配置。

新增：
- code/kern/mm/slub.c：对象分配器实现。
- code/kern/mm/slub.h：公共接口声明。

修改：
- code/kern/mm/pmm.c：在真实物理内存初始化完成后初始化 SLUB。
- code/Makefile：新增 SLUB=0/1 构建选项。

设计范围：
- 小对象按大小分类，同一类别的一页包含多个对象槽。
- 空闲对象槽可以复用。
- 页面最后一个对象释放后，立即将页面归还 Buddy。
- 大对象直接申请连续物理页，释放时归还整个实际分配块。
- 不实现 Linux SLUB 的多核、每 CPU 缓存或无锁优化。

操作要求：
- 直接修改实际文件。
- 保留当前 page_init()、Buddy、Best-Fit 和所有已有测试。
- 保留工作区中尚未提交的 Buddy 测试修正。
- 不修改 struct Page 或 pmm_manager 的公共布局。
- 不修改 tools/ 下的官方评分和注入逻辑。
- 在 lab2 分支工作，未经我逐次明确允许，不执行 commit、amend 或 push。
- 本轮实现模块及必要的基本验证，不提前编写完整 SLUB 测试套件，不改实验报告正文。
- 如果发现与本轮无关的问题，先报告位置和影响，不自行扩大修复范围。

输出要求：
说明对象类别、页内布局、空闲槽管理、满页/部分使用页的状态转换、大对象记录方式，以及如何保证全部释放后底层页数恢复。给出真实构建和基本验证结果，区分已验证与尚未验证内容。


[RELY]

以下来自当前仓库，开始前核对真实定义。

1. 页分配公共接口，位于 code/kern/mm/pmm.h：

struct Page *alloc_pages(size_t n);
void free_pages(struct Page *base, size_t n);
size_t nr_free_pages(void);

#define alloc_page() alloc_pages(1)
#define free_page(page) free_pages(page, 1)

SLUB 通过这些接口取得和归还底层页面，不能直接访问 Buddy 的私有空闲链表。

2. 当前 Buddy 的分配与释放契约

- n>0 的申请实际占用最小的、大小不小于 n 的 2 的幂页数。
- 申请失败返回 NULL。
- free_pages(base, n) 必须传原分配返回的 base 和原请求 n。
- 例如申请 3 页实际占用 4 页，释放仍传原请求 3。
- 不支持部分释放。
- SLUB 需要自行记录释放大对象所需的原页请求数；不能要求 kfree() 的调用者额外提供大小。

3. 地址转换和描述符

extern struct Page *pages;
extern size_t npage;
extern const size_t nbase;
extern uint64_t va_pa_offset;

page2pa(page) 返回物理页起始地址；
PADDR(kva) 将合法内核虚拟地址转换为物理地址；
pa2page(pa) 返回对应的 Page 描述符。

已分配页对应的可访问虚拟地址可由物理地址加 va_pa_offset 得到。PGSIZE 为 4096。pmm.h 中的 KADDR 宏被注释，不能当作可用接口。

4. 可用基础工具

工程提供：
- list_entry_t 及链表操作。
- ROUNDUP、ROUNDDOWN。
- memset、memcpy。
- assert、panic、cprintf。

不得使用宿主 malloc/free，也不能让 SLUB 为管理自身元数据递归调用 kmalloc。

5. 当前初始化顺序

init_pmm_manager()
可选 Buddy 描述符夹具测试
page_init()
check_alloc_page()
可选 Buddy 真实页面测试

SLUB 初始化必须发生在真实 page_init() 和管理器检查之后。

6. 当前构建配置

PMM=best_fit：默认 Best-Fit。
PMM=buddy：普通 Buddy。
PMM=buddy BUDDY_TEST=1：Buddy 专项测试。

Makefile 使用 PMMCFG 决定对象目录，并把配置写入 bin/.pmm_config；配置不同时强制重链共享镜像。新增选项必须进入相同的配置身份机制，不能只增加编译宏而继续复用旧对象或旧镜像。


[GUARANTEE]

在 slub.h 声明并在 slub.c 实现：

void slub_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr);
void slub_check(void);

允许增加模块内部 static 函数、类别表和元数据。

构建行为：
- SLUB 默认为 0。
- PMM=buddy SLUB=1 启用对象分配器。
- SLUB 只接受 0、1。
- SLUB=1 要求 PMM=buddy。
- 本阶段不组合运行 Buddy 专项测试与 SLUB：BUDDY_TEST=1 SLUB=1 应明确报错。
- 原有三个配置保持有效。
- 新增 buddy_slub 配置身份，确保无须 clean 即可正确切换。


[SPECIFICATION]

## 对象分配总体契约

小对象类别为：16、32、64、128、256、512、1024、2048 字节。对于 1..2048 字节请求，请选择能够容纳请求的最小类别。对于大于 2048 字节的请求，则走大对象页分配路径。

返回地址至少按 16 字节对齐。成功返回的区域至少有 size 字节可由调用者自由读写，不能覆盖分配器元数据或其他仍存活对象。不保证返回内存已清零。调用者只能依赖请求的 size 字节，不依赖类别额外空间。

kmalloc(0) 返回 NULL，不申请页面。kfree(NULL) 不执行任何操作。kfree() 的其他输入必须是仍存活的、由 kmalloc() 返回的原指针；本轮不要求支持非法指针、内部指针、重复释放的检测。

单 hart、串行调用；不在中断处理程序中调用。不要求实现并发保护。


## slub_init

Pre-Condition：
当前页管理器为 Buddy，真实物理内存已经初始化；SLUB 尚未初始化。

Post-Condition：
各对象类别及大对象管理状态为空，可以接受 kmalloc/kfree 调用。初始化本身不预分配页面。

Requirements：
初始化只执行一次，不允许通过再次初始化丢弃存活分配。


## 小对象分配

Pre-Condition：
SLUB 已初始化，size 在 1..2048 范围内。

Post-Condition：
返回正确类别的一个可用对象槽；该槽与所有存活对象不重叠，内部计数和空闲状态一致。

Case 1：该类别已有可用槽。
优先复用现有槽，不额外申请页面。

Case 2：该类别没有可用槽。
向 Buddy 申请一个页面，在页内建立元数据与对象槽，再交付其中一个槽。元数据和对齐空间不得被计入可用对象区域。

Case 3：底层页申请失败。
返回 NULL；不泄漏页面，不改变已有对象内容，不破坏原有类别状态。

Requirements：
- 每个小对象 slab 恰好占用一个底层页面。
- 每个类别都必须至少能在一页中容纳一个对象。
- 同类别的小对象能够共享页面，不能把每次 kmalloc 都实现为独立整页分配。
- 元数据放静态类别表和所申请页面内部，不额外依赖对象分配。
- 空闲槽可以保存内部链指针，但不能占用存活对象的有效字节。
- 能追踪满页和部分使用页，以便释放后重新使用。


## 小对象释放

Pre-Condition：
ptr 为仍存活的小对象原指针。

Post-Condition：
该对象槽可再次使用，存活对象数相应减少；其他对象内容保持不变。

Case 1：释放后页面仍有存活对象。
保留该页；若它之前是满页，现在应重新成为可分配页面。

Case 2：释放后页面没有存活对象。
先从所有 SLUB 管理结构移除该页，再以原页请求 1 归还 Buddy。

Requirements：
- 最后一个对象释放后立即归还空页，不保留空 slab 缓存。
- 归还页面后不能继续读取或修改其中的元数据。
- 对象所属页、类别和槽位置必须可以从有效原指针恢复。


## 大对象分配

Pre-Condition：
SLUB 已初始化，size>2048。

Post-Condition：
成功时返回至少 size 字节的连续虚拟内存，地址按 16 字节对齐；保存释放所需的信息。

Case 1：所需大小可表示且底层分配成功。
为元数据、对齐空间和有效载荷计算原始页请求数，通过 alloc_pages() 申请，记录原始 Page 指针及原页请求数。记录值必须符合 Buddy 的原指针、原请求释放契约。

Case 2：size、附加元数据、对齐或页数计算超出支持范围。
返回 NULL，不发生整数溢出、截断或非法移位。

Case 3：底层连续页不足。
返回 NULL，已有对象及计数不变，不泄漏元数据资源。

Requirements：
- 所有计算检查必须在实际写入元数据之前完成。
- 元数据与用户有效载荷不重叠。
- 可以在分配块首页保存头部，并从返回指针定位它；需明确保证这种定位方式对全部大对象都成立。
- 不承诺超过当前内存容量的请求能够成功。


## 大对象释放

Pre-Condition：
ptr 为仍存活的大对象原指针。

Post-Condition：
移除其 SLUB 管理记录，使用保存的原始 Page 指针和原页请求数调用 free_pages()，完整归还 Buddy 实际占用的块。

Requirements：
不能只根据用户请求字节数重新猜测原页请求数；不能忘记元数据、对齐造成的额外占用。页面归还后不再访问该块内元数据。


## slub_check

Pre-Condition：
SLUB 已初始化，调用期间没有分配与释放并发执行。

Post-Condition：
检查完成，不改变对象和页面所有权。

至少检查：
- 各 slab 属于有效类别，页内元数据和对象区域范围合法。
- 对象槽起点和步长满足对齐要求。
- 空闲槽全部位于对应对象区域，并落在合法槽边界。
- 空闲链不存在重复节点或环，遍历有合理上界。
- 空闲槽数与存活槽数之和等于页面容量。
- 满页、部分使用页及所属管理链状态一致。
- 正常调用结束后不存在仍被管理的空 slab。
- 大对象记录的页范围、有效载荷和原页请求数相符。

Requirements：
检查不能是无条件输出成功的空函数。结构检查不代表完整行为测试已经通过。


## 初始化接入与资源恢复

启用 SLUB 时，在真实页初始化和页管理器检查完成后调用 slub_init()，输出可以识别的启用信息。关闭 SLUB 时，不执行对象分配初始化，不改变原有 Buddy 或 Best-Fit 行为。只要全部 kmalloc 分配都通过 kfree 归还，底层 nr_free_pages() 就应恢复到这些分配开始前的值。

不得假设不同构建配置具有相同的初始空闲页数。


## 本轮基本验证

在临时副本或可清理的验证入口进行：

1. 初始化不消耗页面；
   kmalloc(0)、kfree(NULL) 不改变状态。

2. 连续申请两个 32 字节对象：
   两者不重叠且共享一个 slab 页面；释放一个后页面仍保留，释放最后一个后底层页数恢复。

3. 申请 2048 字节对象并写满请求范围，再释放；
   验证该最大类别没有因页头占用而越界。

4. 申请 8193 字节的大对象，写入并回读全部请求字节；
   验证元数据未被覆盖，kfree 能完整恢复底层空闲页数。

5. kmalloc((size_t)-1) 返回 NULL，不改变已有资源。

6. 在有存活对象和全部释放后的状态分别运行 slub_check。

这些只是实现轮的基本验证，完整的类别边界、跨 slab 复用、耗尽与失败恢复测试留到下一轮。

构建回归：
best_fit → buddy → buddy_slub → buddy_test → buddy → best_fit。

验证不 clean 切换正确，普通配置不残留 SLUB 初始化；Buddy 专项测试仍运行原有 12 项。非法配置应明确报错。QEMU 使用有界等待并检查实际日志；若缺少成功证据或出现 panic，不能只凭超时退出判断通过。

本轮结束交付实现、接口契约和实际验证记录，不提交代码，不提前编写完整 SLUB 专项测试套件。
~~~~

##### 追加提示词 - 1

~~~~markdown
我看了一下你的实现，发现有些问题：首先大对象链表缺少最终计数核对。slub.c (line 339) 只检查了 large <= slub_large_count，在遍历后没有断言二者相等，这会导致在记录丢失、计数偏大时，仍有可能打印出 consistent。
其次，也没有检查底层块的完整物理范围。slub.c (line 261、327) 检查了页内布局及部分地址关系，但并没有完整验证 slab 页面和大对象实际占用块均位于可管理范围，这一轮要求的范围检查尚未落实完整。你再改一下
~~~~

##### 追加提示词 - 2

~~~~markdown
还是有问题。范围检查要先验证整数地址、页号及描述符偏移，确认合法后再调用地址转换函数或做指针运算。块长度用“实际占用页数 ≤ 描述符总数 − 起点偏移”检查，避免计算越界终点；大对象实际占用按 Buddy 向上取整，同时检查对应对齐。检查路径中的大小相加、向上对齐也要先防溢出。
~~~~


## 提示词 6

~~~~markdown
[PROMPT]

任务：
为当前简化 SLUB 对象分配器实现独立专项测试，检验 kmalloc/kfree 的类别选择、对象隔离、页面复用、大对象管理、资源不足和完整资源恢复。

新增：
- code/kern/mm/slub_test.c
- code/kern/mm/slub_test.h

修改：
- code/kern/mm/pmm.c：在 slub_init() 之后接入测试。
- code/Makefile：新增 SLUB_TEST=0/1 配置。

本轮重点是测试，不重写分配器。

操作要求：
- 直接修改实际文件，保留已有未提交改动。
- 不修改 Buddy、Best-Fit 和现有测试。
- 原则上不修改 slub.c；若发现实现缺陷，先说明最小复现、原因和影响，等待我决定修复方式。
- 不修改 tools/ 下的官方评分、测试和注入逻辑。
- 在 lab2 分支工作，未经我逐次明确允许，不执行 commit、amend 或 push。
- 暂不编写实验报告正文。
- 所有释放必须针对尚未释放的原分配指针。不测试契约未承诺的内部指针、重复释放或非法指针。

输出要求：
交付可重复运行的测试及逐项结果。说明每项测试的断言能识别什么错误。区分代码检查、实际运行和未覆盖场景，不虚构成功结果。


[RELY]

1. SLUB 公共接口，位于 code/kern/mm/slub.h：

void slub_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr);
void slub_check(void);

slub_init() 已由 pmm_init() 在真实页初始化、页管理器检查之后调用，不能在测试中再次初始化。

2. 当前行为契约

- kmalloc(0) 返回 NULL，不消耗页面。
- kfree(NULL) 不改变状态。
- 返回地址至少按 16 字节对齐。
- 返回内存不保证清零。
- 调用者只读写原请求 size 字节。
- 1..2048 字节请求进入最小可容纳对象类别。
- 类别为 16/32/64/128/256/512/1024/2048。
- 每个小对象 slab 恰好占一页。
- 优先使用已有空闲槽，没有槽才申请新页面。
- 页面最后一个对象释放后立即归还 Buddy。
- 大对象记录原始 Page 指针和原页请求数，kfree 不需要调用者提供大小。
- 不支持并发或中断上下文调用。

3. 当前实现的布局事实

小 slab：
- 页首元数据向上对齐后占 48 字节。
- 槽步长为对应类别大小。
- 各类别每页容量依次为：253、126、63、31、15、7、3、1。
- 类别分别维护 partial/full 链。
- 空闲槽用自身前一个指针宽度的空间串联；存活对象内容不能用于管理元数据。

大对象：
- 头部向上对齐后占 48 字节。
- 返回地址为块首虚拟地址 + 48。
- 原页请求数为 ceil((size+48)/4096)。
- Buddy 再将原页请求向上取整到 2 的幂，作为实际占用。
- kfree 使用保存的原指针、原页请求数归还完整块。

以上容量和头部大小属于当前实现的布局验收依据，应与通用行为测试区分。不要复制私有结构体到测试文件，不要通过包含 slub.c 来访问内部状态。

4. 页分配接口

struct Page *alloc_pages(size_t n);
void free_pages(struct Page *base, size_t n);
size_t nr_free_pages(void);

页描述符总数为 npage-nbase，当前框架最多为 32768。可以直接申请单页，制造真实的底层内存耗尽；每次必须以原返回指针和原请求 1 释放。

5. 当前配置

默认：PMM=best_fit
普通 Buddy：PMM=buddy
Buddy 测试：PMM=buddy BUDDY_TEST=1
普通 SLUB：PMM=buddy SLUB=1

Makefile 使用 PMMCFG 隔离对象文件，并通过 bin/.pmm_config 判断共享镜像是否需要重链。SLUB 与 BUDDY_TEST 当前互斥。


[GUARANTEE]

新增并声明：

void slub_run_tests(void);

允许增加测试文件内部 static 函数、对象记录和辅助数据。

构建配置：
- SLUB_TEST 默认为 0，只允许 0 或 1。
- SLUB_TEST=1 要求 PMM=buddy、SLUB=1、BUDDY_TEST=0。
- 测试配置使用独立身份 buddy_slub_test，同时参与对象目录
~~~~

