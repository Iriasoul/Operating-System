# Lab1：使用提供的课程框架

当前目录保留小组提供的启动代码与基础库。只修改原 Makefile 和 libs/sbi.c，补入缺失的本地 grade 脚本和验证工具；入口、初始化、console、stdio、printfmt、string 与链接脚本保持原样。

## 文档

- [按模板编写的实验报告](report.md)
- [最终提示词](prompts.md)
- [三人分工](../docs/分工.md)
- [框架对照与验收记录](../docs/实验一验收.md)

## 运行和调试

在已经配置 RISC-V GCC、binutils、GDB、QEMU 和 Python 3 的 Linux/WSL 中执行：

~~~bash
make
make qemu
~~~

启动后输出原框架的 (THU.CST) os is loading ...，随后无限循环。退出 QEMU：Ctrl+A，松开后按 X。

~~~bash
# 终端一
make debug
# 终端二
make gdb
# 或使用预设的跟踪脚本
riscv64-unknown-elf-gdb -q -x tools/boot.gdb
~~~

默认端口为 1234；可同时使用 make debug GDB_PORT=1235 和 make gdb GDB_PORT=1235。boot.gdb 固定使用默认端口。

## 验证

~~~bash
make check       # 启动消息和持续运行
make check-gdb   # 复位、固件、原内核、栈、初始化范围
make grade       # 清理重建后执行以上四项本地检查
~~~

提供的代码中没有 tools/grade.sh，所以本次补入本地版本。它不会给出或模拟官方课程得分。

当前 QEMU 7.0.0 的默认 OpenSBI 是动态固件。原 -device loader 参数使固件下一阶段地址为 0；改为 -kernel 后，启动地址正确变为 0x80200000。该修改只影响加载方式，运行的是本目录的原框架镜像。

libs/sbi.c 的修改是显式描述 SBI ABI 寄存器，以及补齐已被 cons_getc 调用的字符输入包装函数。实际运行验证覆盖字符输出和启动过程，没有对字符输入或定时器作交互测试。

## 报告证据

validation 保存完整编译、QEMU、GDB、本地 grade 和首次启动失败日志。images 内为真实输出日志渲染的图片，报告已经嵌入；不是桌面终端截图。

~~~bash
python3 tools/record_validation.py
~~~

上述命令重新记录完整验证日志。Windows 下有 Pillow 时可运行 python tools/render_validation.py 重新生成输出图。它使用 Windows 的 Consolas 字体，不是内核编译依赖。
