# lab5 课程用户态测试集（课程组提供 · 评分参考）

> 版本：v2（2026-11，随 lab5 增量包 v2 修订：新增 `echo.c` 参数栈规整与 16 字节对齐校验程序；提供支持空格分词的 `gifts/user/sh.c` 与 `gifts/Makefile.upgrade`；补充前置能力清单、`cowpair` 计数机制说明及 `LAB5_COW_BUDGET` 单进程记账建议）。

## 一、构建方式

与 lab2–lab4 相同的内嵌构建方式：

1. 将本目录的 `zerocopy.c`、`cowpair.c`、`echo.c` 拷贝到 `user/`；
2. 将 `gifts/kernel/syscall.h`、`gifts/user/user.h`、`gifts/user/usys.pl`、`gifts/user/sh.c` 覆盖到工程对应目录（新增 `SYS_pmc = 26` 及支持空格分词传参的最小 Shell）；
3. 在 `Makefile` 的 `UPROGS` 中追加 `zerocopy cowpair echo`（见增量包 `gifts/Makefile.upgrade`）。

## 二、前置能力清单

以下能力全部实现后再运行对应测试：

| 能力 | 最小语义 | 依赖它的测试 | 缺失时的现象 |
| :--- | :--- | :--- | :--- |
| 物理页引用计数与 `kfree` 归零回收 | `kalloc` 置计数为 `1`；`kfree` 递减计数，仅当计数归 `0` 时执行 `memset(pa, 0x5a, PGSIZE)` 并归还空闲池 | 全部 | 共享物理页被提前覆写 `0x5a` 导致子进程或 Shell 崩溃 |
| `uvmcopy` 只读共享与 `cow_alloc` 写缺页分离 | 父子可写页均清除 `PTE_W` 并置 `PTE_COW`（跳过 `PTE_V == 0` 保护页），刷新 TLB；`usertrap` 捕获 `scause == 15` 分离页面；`refcnt == 1` 时原地恢复 `PTE_W`（不计入 `pmc(0)`） | `cowpair`、`zerocopy` | `sh` 在 `fork()` 返回后首次写栈即触发 `scause = 15` 被终止，或 `cowpair` 的 `copies delta` 大于 `2` |
| `copyout` 与 `uvm_check_range` 识别 `PTE_COW` | 内核态写用户缓冲区前，若目标页 `PTE_W == 0` 且带 `PTE_COW`，先调用 `cow_alloc` 分离物理页 | `sh` 交互与全部测试 | `sh` 首次 `fork()` 后 `.bss` 段 `buf` 页变为 COW 只读，下一轮 `gets(buf)` 调用 `read` 返回 `-1` 导致 `sh` 死循环 |
| `pmc(which)`（`SYS_pmc = 26`） | `pmc(0)` 返回开机以来实际分配新页并拷贝的 COW 次数；`pmc(1)` 返回当前 `refcnt > 1` 的共享物理页数 | `zerocopy`、`cowpair` | 链接失败或报计数断言失败 |
| `exec` 参数栈规整装配 | 提取 `argv[]` 字符串压入新用户栈顶，设置 `ustack[argc] = 0` 哨兵与 16 字节对齐 `sp`，设置 `tf->a1 = sp` 并由 `kexec` 返回 `argc`（写入 `tf->a0`） | `echo hi` | `echo hi` 报 `T5-3 FAIL: invalid argc=0` 或未对齐错误 |

## 三、测试与判据

| 测试命令 | 测试覆盖范围 | 通过标准 |
| :--- | :--- | :--- |
| `zerocopy` | `fork` 后立即 `exec("hi")` 的延迟拷贝断言 | 实际物理页复制增量 `delta <= 2`，打印 `T5-1 PASS: fork-immediately-exec ~zero copy` |
| `cowpair` | 父子配对写入、数据隔离与 `refcnt == 1` 原地复用对账 | 共享计数大于 `0`、父子读回各自数据无串扰、栈页与堆页合计触发恰好 `2` 次物理页复制（`delta == 2`），打印 `T5-2 PASS: pair-write exact 2 copies, data isolated` |
| `echo hi` | `exec` 用户栈 `argc`/`argv` 装配、哨兵与 16 字节对齐校验 | 第一行打印 `hi`，第二行打印 `T5-3 PASS: argc=2 argv[0]=echo aligned` |

说明：`LAB5_COW_BUDGET` 复制配额耗尽后的降级行为建议按**单进程**累计拷贝次数统计，并在达到阈值时采取“打印一次内核告警后继续完成拷贝”的容错降级策略，避免中断 `lab4` 的 `bench` 多轮并发回归测试。
