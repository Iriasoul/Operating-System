# Operating-System

NKU《操作系统》课程代码仓库 - 2026 Fall。

本次作业使用小组提供的 **lab1/** 课程框架。此前单独编写的 labcodes/lab1 内核已删除，实验报告按照根目录的《实验报告模板.md》重写。

- [实验一报告](lab1/report.md)
- [最终提示词与规格](lab1/prompts.md)
- [框架运行说明](lab1/README.md)
- [三人分工](docs/分工.md)
- [框架修改与验收记录](docs/实验一验收.md)

在已有 WSL 环境中验证：

~~~powershell
wsl -d CompilerLab -- bash -lc 'cd /mnt/c/Users/18695/Desktop/Operating-System/lab1 && make && make qemu'
~~~

在 lab1 目录运行 make grade 可执行补充的本地启动检查。它验证本次框架，输出 4/4 PASS，不代表官方评分。完整运行日志和输出图分别位于 lab1/validation 和 lab1/images。

原始报告模板保持不变。三位成员均填写 Codex；学号和具体模型版本尚未提供，已在报告中注明。提交仓库：[Iriasoul/Operating-System](https://github.com/Iriasoul/Operating-System)，分支 main。
