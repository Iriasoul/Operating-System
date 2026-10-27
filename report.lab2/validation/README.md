# Lab 2 验证材料说明

Best-Fit 尾插修复和此前专项测试的日志按原输出归档；补充 SLUB 满页复用用例后，重新运行了 SLUB 全套测试及官方评分，新增输出见末节。

## 本轮 Best-Fit 验证

运行环境：Windows 上的 WSL 2 / CompilerLab，Ubuntu 22.04.5 LTS；GCC 15.1.0；QEMU 4.1.1，程序路径为 `/home/compiler/.local/opt/qemu-4.1.1/bin/qemu-system-riscv64`。

代码基线：`lab2` 分支提交 `176f6aae68c566627b9d99e8fa5b708cc49ca6de`。本次提交在该基线上包含 Best-Fit 与 First-Fit 尾插修复、回归工具、任务标记调整及 SLUB 满页复用测试。Best-Fit 修复前复现使用未加入两处 `break` 的临时副本。

下列命令均在 WSL 的 `/mnt/c/Users/18695/Desktop/Operating-System/code` 下、加载 `~/.config/compilerlab/riscv-env.sh` 且 `QEMU` 指向上述 4.1.1 程序后执行。命令列用于对应测试入口，不是完整的历史终端录屏。

| 归档文件 | 原始来源（相对仓库根目录） | 对应命令 | 结果 |
|----------|--------------------------|----------|------|
| [best-fit-tail-before-init.log](best-fit-tail-before-init.log) | `code/obj/best_fit_tail_validation/before-init.log` | 修复前副本：`python3 tools/check_best_fit_tail.py --case init` | `x != y` 断言失败 |
| [best-fit-tail-before-free.log](best-fit-tail-before-free.log) | `code/obj/best_fit_tail_validation/before-free.log` | 修复前副本：`python3 tools/check_best_fit_tail.py --case free` | `x != y` 断言失败 |
| [best-fit-tail-after.log](best-fit-tail-after.log) | `code/obj/best_fit_tail_validation/run-all.log` | 修复后：`python3 tools/check_best_fit_tail.py` | 初始化和释放两项均 PASS，原有自检成功 |
| [qemu411-grade.log](qemu411-grade.log) | `code/obj/best_fit_tail_validation/grade.log` | 修复后：`make grade` | 两项官方检查 OK，20/20 |

四份归档与对应原始文件的 SHA-256 均一致。`code/obj/` 被 Git 忽略；此处日志和 `report/images/` 中的截图未被忽略，需要与代码、报告一起纳入最终提交。

当前环境截图已嵌入 [报告](../report.md)：默认启动 `lab2-qemu411-startup.png`、官方评分 `lab2-qemu411-grade.png`、尾插回归 `lab2-best-fit-tail-tests.png`，均来自组员提供的实际运行截图。

## 同环境下的其他验证

[qemu411-buddy.log](qemu411-buddy.log) 和 [qemu411-slub.log](qemu411-slub.log) 保留此前 QEMU 4.1.1 环境下 Buddy 12/12、SLUB 11/11 的运行输出。运行准备见报告 5.1 节，Buddy 与 SLUB 的命令、截图和当前结果分别见 5.4、5.5 节。

## SLUB 满页复用增强验证

SLUB 增强验证使用上述基线及已有修复，并在 `code/kern/mm/slub_test.c` 新增 L11 用例。原有 10 个测试保留，满页复用作为第 11 项，最终全空检查顺延为第 12 项。

在同一 CompilerLab / QEMU 4.1.1 环境的 `code/` 目录执行构建 `make PMM=buddy SLUB=1 SLUB_TEST=1`，再运行 `"$QEMU" -machine virt -nographic -bios default -kernel bin/ucore.img`。运行器等待最终 12 项成功标记，检查没有 FAIL 或 panic 后结束 QEMU；超时本身不作为通过依据。随后切回 `make PMM=best_fit` 并执行 `make grade`。

| 归档文件 | 原始来源（相对仓库根目录） | 结果 |
|----------|--------------------------|------|
| [qemu411-slub-full-reuse-build.log](qemu411-slub-full-reuse-build.log) | `code/obj/slub_full_reuse_validation/build.log` | 增强测试构建成功 |
| [qemu411-slub-full-reuse.log](qemu411-slub-full-reuse.log) | `code/obj/slub_full_reuse_validation/run.log` | 12/12 PASS；满页→部分使用→满页→全空均 consistent；原槽复用且不增页，幸存数据完整 |
| [qemu411-full-reuse-grade.log](qemu411-full-reuse-grade.log) | `code/obj/slub_full_reuse_validation/grade.log` | 官方评分仍为 20/20 |

新增日志与对应原始文件一致。报告中的 [SLUB 截图](../images/lab2-slub-tests.png) 已替换为组员提供的增强后实际运行截图，显示满页槽位复用检查及 12 项全部通过；截图按原文件复制，未修改内容，SHA-256 与原文件一致。此前 SLUB 11 项日志保留用于核对历史结果，当前 12 项结果由新截图和新增日志共同记录。本次镜像初始空闲页为 31859，使用运行内动态基准验证全部归还，不沿用此前镜像的固定计数。

## First-Fit 参考实现尾插修复

在上述本地版本上，为 `default_init_memmap()`、`default_free_pages()` 的尾插分支各增加一个 `break`。回归运行器新增 `--manager first_fit`，仅在临时副本中选择该管理器，使用 `tools/test_first_fit_tail.c` 检查初始化尾插链接、释放尾插合并、耗尽及完整恢复。运行命令为 `python3 tools/check_best_fit_tail.py --manager first_fit`，环境仍为 CompilerLab / QEMU 4.1.1。

修复前验证使用保留的旧 `default_pmm.c` 临时副本，分别执行 `--case init`、`--case free`。修复后执行完整 First-Fit 回归、原有自检，再复验 Best-Fit 回归和 `make grade`，最后重新构建默认 Best-Fit 镜像。

| 归档文件 | 原始来源（相对仓库根目录） | 结果 |
|----------|--------------------------|------|
| [first-fit-tail-before-init.log](first-fit-tail-before-init.log) | `code/obj/first_fit_tail_validation/before-init.log` | 前驱链接断言失败 |
| [first-fit-tail-before-free.log](first-fit-tail-before-free.log) | `code/obj/first_fit_tail_validation/before-free.log` | 相邻块未合并，连续 3 页申请失败 |
| [first-fit-tail-after.log](first-fit-tail-after.log) | `code/obj/first_fit_tail_validation/run-all.log` | 两项 PASS，First-Fit 原有检查成功 |
| [best-fit-after-first-fit-fix.log](best-fit-after-first-fit-fix.log) | `code/obj/first_fit_tail_validation/best-fit-after.log` | Best-Fit 两项 PASS，原有检查成功 |
| [qemu411-first-fit-fix-grade.log](qemu411-first-fit-fix-grade.log) | `code/obj/first_fit_tail_validation/grade.log` | 官方评分 20/20 |

[First-Fit 回归截图](../images/lab2-first-fit-tail-tests.png) 展示运行命令、QEMU 4.1.1、两项 PASS 和原有页分配检查成功，已嵌入报告 5.3 节。
