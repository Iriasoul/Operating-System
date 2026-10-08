# Operating-System · Lab1

本分支为实验一提交分支 `lab1`，使用小组提供的课程代码框架。交付物按课程要求放在两个文件夹中：

~~~text
code/                     实现后的源代码、Makefile 和调试工具
report/
  report.md               按课程模板编写的实验报告
  prompt.md               本实验提示词汇总
  images/                 报告引用的测试结果图片
  validation/             图片对应的原始运行日志
  support/                三人分工与框架修改记录
  实验报告模板.md          用户提供的原始模板
~~~

- [实验报告](report/report.md)
- [提示词汇总](report/prompt.md)
- [编译、运行与调试说明](code/README.md)
- [三人分工](report/support/分工.md)

在配置好 RISC-V 工具链、QEMU 和 GDB 的 Linux/WSL 环境中运行：

~~~bash
cd code
make
make qemu
# 退出 QEMU：Ctrl+A，松开后按 X
make grade
~~~

本地验证包含启动消息、启动链、栈及初始化四项检查，不代表官方评分。报告中的四张图片为用户提供的真实终端截图；自动验证的原始日志保存在 report/validation/。

三位成员为2412449-石晨昊、2410966-王策、2410668-吕昊远，均使用 Codex，底层模型均为 GPT6.1sol；分工已由小组确认。提交仓库：[Iriasoul/Operating-System · lab1](https://github.com/Iriasoul/Operating-System/tree/lab1)。
