# Lab1：使用提供的课程框架

本目录包含补全后的课程框架。原始入口、初始化代码与基础库保留；修改 Makefile 的加载与调试参数、libs/sbi.c 的调用约束和输入接口，补充本地验证工具。

- [实验报告](../report/report.md)
- [提示词汇总](../report/prompt.md)
- [三人分工](../report/support/分工.md)
- [框架修改与验收记录](../report/support/实验一验收.md)

## 运行和调试

在配置好 RISC-V GCC、binutils、GDB、QEMU 和 Python 3 的 Linux/WSL 中，从本目录执行：

~~~bash
make
make qemu
~~~

输出 `(THU.CST) os is loading ...` 后进入无限循环。退出 QEMU：Ctrl+A，松开后按 X。

~~~bash
# 终端一
make debug
# 终端二
make gdb
# 或使用预设跟踪脚本
riscv64-unknown-elf-gdb -q -x tools/boot.gdb
~~~

默认端口为 1234。更换端口时同时使用 `make debug GDB_PORT=1235` 与 `make gdb GDB_PORT=1235`；boot.gdb 固定使用默认端口。

## 验证

~~~bash
make check       # 启动消息和持续运行
make check-gdb   # 复位、固件、内核、栈、初始化范围
make grade       # 清理重建后执行四项本地检查
~~~

提供的代码缺少 Makefile 引用的 tools/grade.sh，因此补入本地验证脚本；它不模拟官方课程评分。

本次使用的 QEMU 7.0.0 默认 OpenSBI 为动态固件，原 `-device loader` 参数使下一阶段地址为 0。改用 `-kernel` 后，启动地址为 0x80200000，仍运行本框架生成的镜像。libs/sbi.c 显式描述 SBI ABI 寄存器，并补齐 cons_getc 使用的输入接口。实测覆盖字符输出与启动过程，没有进行字符输入或定时器交互测试。

## 报告证据

验证工具将日志写入 [../report/validation/](../report/validation/)。用户提供的真实终端截图保存在 [../report/images/](../report/images/)，code/ 中保留代码和构建工具。

~~~bash
python3 tools/record_validation.py
~~~

上述命令重新构建、运行 QEMU、执行本地 grade 并保存完整输出。报告已使用用户提供的四张真实终端截图。

可选的日志预览工具 `python tools/render_validation.py` 需要 Windows 的 Pillow 与 Consolas 字体，输出到 report/validation/rendered/，不会覆盖 report/images/ 中的真实截图。它不属于内核编译依赖。
