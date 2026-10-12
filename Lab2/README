lab1 初始代码骨架(自动生成)
======================
你所拥有的文件: kernel/kernel.ld(lab1 注释版)、kernel/riscv.h(底层硬件宏定义，完整)、
               Makefile、kernel/course_sid.h(个性化参数文件，请勿修改)。
需要你编写的代码: kernel/ 目录下五个核心文件 —— entry.S / start.c / console.c /
                printf.c / main.c。
第一步: 仔细阅读《实验说明书(lab1)》第 0 节的代码导读路线，完成设计笔记后再动手编写代码。
初始预期现象: 骨架代码能够"构建成功"，但会输出一条链接器警告
(warning: cannot find entry symbol _entry)——此时内核尚未包含具体的启动代码，
直接运行 make qemu 屏幕无输出。正确编写 entry.S 后该警告将消失。

======================
推荐 git 工作流(全学期常用基础指令)
======================
  git init && git add -A && git commit -m "baseline"   # 收到初始基线后，进行首次提交
  git tag labN-start                                    # 每轮合并增量包后打上开始标签
  git add -A && git commit -m "..."                     # 开发过程中阶段性提交
  git archive --format=zip -o 提交-labN-<学号>.zip labN-submit   # 每轮实验验收前打包
推荐做法: 建议创建私有远程仓库(校内代码平台/GitLab/Gitee 等)进行代码备份。
若代码出现异常可通过 git reset --hard labN-start 恢复初始状态；定位"从哪次提交开始引入缺陷"可使用 git bisect。
commit 历史虽不作统一打分，但在现场验收进行代码溯源和设计答辩时，是最清晰的自主开发记录佐证。
