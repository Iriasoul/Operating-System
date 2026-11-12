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

`kern/libs/error.h` 中定义 #define E_NO_MEM 4，因此内存不足返回 -E_NO_MEM。

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


## 提示词 2：

~~~~markdown

~~~~


## 提示词 3：

~~~~markdown

~~~~

