# lab4 课程用户态测试集（课程组提供 · 评分参考）

> 版本：v2（2026-10，随 lab4 增量包 v2 修订：bench 去除管道依赖；负载改为纯计算；
> 子进程到截止时刻自行退出，不再依赖 kill；断言全部改为整数运算；补充前置能力清单与策略适用表）。

## 一、构建方式

与 lab2/lab3 相同的内嵌构建方式：

1. 将本目录的 `common.h`、`common.c`、`bench.c`、`lottery_test.c`、`stride_test.c` 拷贝到 `user/`；
2. 在 `Makefile` 的 `UPROGS` 中追加 `bench lottery_test stride_test`（见增量包 `gifts/Makefile.upgrade`）；
   lab3 结束时的构建规则在 `user/common.c` 存在时会自动把它链接进每个用户程序，无需另加规则；
3. 可选：在 `UFLAGS` 中加入 `-DSID=<你的学号>`，使 `stride_test` 使用与你学号对应的票数；
   不加时按 `SID=0` 计算（票数 5 与 15），判据不变。验收时课程组按学号注入。

## 二、前置能力清单

以下能力全部实现后再运行测试；缺少任何一项，测试会报 `FAIL` 或立即出错，而不是误报通过。

| 能力 | 最小语义 | 依赖它的测试 | 缺失时的现象 |
|---|---|---|---|
| `fork` / `exit` / `wait`（含退出码写回） | lab2/lab3 已实现；`wait(&st)` 须写回子进程退出码 | 全部 | `bench` 报"退出码异常" |
| `pause(n)`（SYS_pause = 13） | 当前进程休眠 n 个时钟 tick 后返回；用 `sleep/wakeup` 实现，时钟中断处理中对 tick 计数并唤醒等待者 | 全部 | 立即返回则观测窗口为 0，报"总 CPU 采样过少" |
| `schedstat(pid, &cpu, &slice)`（23） | 见 `support/schedstat.patch`；`pid = -1` 表示自身；目标不存在或已退出返回 -1 | 全部 | 报"schedstat 采样失败" |
| `time_rdtime()`（24） | 返回 `time` CSR 的当前值（`r_time()`），单位为硬件计时器计数，不是 tick | 全部 | 截止时刻计算错误，子进程提前退出或测试超时 |
| `setickets(n)`（25） | 设置当前进程票数，`n < 1` 返回 -1；设定后覆盖策略的默认票数 | `lottery_test`、`stride_test` | 份额断言失败 |
| `kill(pid)`（6） | **可选**。实现后子进程被提前回收、测试结束更快；未实现时子进程在截止时刻自行退出 | 无硬性依赖 | 无（仅多等待约 60 tick） |

`cpu_ticks` 的计数点：时钟中断**从用户态**进入内核（`usertrap` 的时钟分支）时，对当前进程加 1。
内核态被时钟中断打断不计数，因此测试负载均为不发起系统调用的纯计算循环。

## 三、测试与判据

| 测试程序 | 适用策略 ID | 测试内容 | 通过标准 |
|---|---|---|---|
| `bench` | 全部（0–9） | ① 100 轮 `fork → exit → wait` 往返；② 8 个混合负载子进程并发运行后逐个回收 | 输出 `RESULT avg_fork_wait = <n>` 与 `RESULT concurrent_children = 8`，以 `PASS: bench` 结束 |
| `lottery_test [t1 t2]` | 3、4、5 | 两个纯计算子进程按票数 t1:t2（默认 60:20，即 3:1）竞争 200 tick | 输出 `RESULT lottery_ratio = <c1>:<c2>`；c1 占比与 t1/(t1+t2) 相差不超过 15 个百分点 |
| `stride_test` | 6、7 | 两个纯计算子进程，票数 1:3，6 个窗口各 50 tick | 输出 `RESULT stride_ratio = <c1>:<c2>`；c2/c1 在 2.4–3.6 之间，且每个窗口两者都获得 CPU |

策略 ID 0–2（多级反馈队列，MLFQ）与 8–9（优先级）未提供通用策略测试程序，需用 `schedstat` 自行编写观测程序：
MLFQ 观察纯计算进程的 `last_slice_ticks` 是否呈现与队列级数相同的档位；
优先级策略观察高、低优先级进程的 CPU 占用边界，老化变体（ID 9）观察低优先级进程在等待后能获得 CPU。

## 四、运行时长与异常判断

- 一个时钟 tick 的长度由你的时钟中断周期决定（参考实现约 0.1 秒）。`lottery_test` 约需 260 tick，
  `stride_test` 约需 360 tick；时钟周期较长时可达数分钟，属正常现象。
- `bench` 第一部分通常在数秒内完成。若长时间停在第一部分无输出，优先检查 `wait`/`exit` 之间是否存在丢失唤醒。
- 测试结束后应回到 shell 提示符。若打印 `PASS`/`FAIL` 后不返回提示符，检查 `wait` 对已退出子进程的回收。
