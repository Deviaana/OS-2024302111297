# Lab5 设计笔记 - 写时复制 fork、引用计数与 exec 参数栈

学号：`2024302111297`

个性化参数：`LAB4_POLICY_ID=7`（步长调度）、`LAB5_REFCNT_SCHEME=1`
（全局物理页引用计数数组）、`LAB5_COW_BUDGET=193`、
`LAB5_KILL_SEMANTICS=1`（完成当前复制后退出）、`LAB2_TICK=2`、
`LAB3_GUARD_PAGES=2`、`LAB3_MEMCAP_DELTA=1`。

本工程以真实的 Lab4 抢占式调度内核为基线，保留其每进程锁、`wait_lock`、
`swtch`、`scheduler`、用户态时钟抢占和步长调度；在此基础上实现 COW fork、
物理页引用计数、`pmc` 观测接口和 `exec` 参数栈。

## 1. COW 状态转换

以可写数据页 `PA_0` 为例，`refcnt` 表示该物理页被多少张用户页表映射。

### fork 刚完成

```text
父 PTE: PA_0, PTE_W=0, PTE_COW=1 --+
                                    +--> PA_0, refcnt=2
子 PTE: PA_0, PTE_W=0, PTE_COW=1 --+
```

`uvmcopy` 不复制物理页内容，只清除父子 PTE 的 `PTE_W`、设置 `PTE_COW`，
并执行一次 `kref_inc(PA_0)`。代码段继续以 `R+X+U` 权限共享，不带
`PTE_COW`，因此非法写代码段会被拒绝。

### 第一次写入

```text
写入方 PTE: PA_1, PTE_W=1, PTE_COW=0 --> PA_1, refcnt=1
另一方 PTE: PA_0, PTE_W=0, PTE_COW=1 --> PA_0, refcnt=1
```

`cow_alloc` 在 `refcnt > 1` 时分配 `PA_1`，复制整页并修改写入方 PTE，
随后 `kfree(PA_0)` 将旧页计数减为 1。`pmc(0)` 只统计这条真实复制路径。

### 另一方随后写入

`cow_alloc` 发现旧页 `refcnt == 1`，说明其他共享者已经分离或退出。
此时原地清除 `PTE_COW`、恢复 `PTE_W` 并执行 `sfence_vma()`，不分配新页，
也不增加 `pmc(0)`。

## 2. 引用计数与锁

`kernel/kalloc.c` 使用 `refcnt[(pa-KERNBASE)/PGSIZE]` 保存权威计数。
位图、引用计数和 COW 累计复制次数共用一个 `kmem_lock()` 临界区：

- `kalloc` 成功取页后设置 `refcnt=1`；
- `uvmcopy` 每成功建立一条共享映射就 `kref_inc`；
- `cow_alloc` 先复制新页，再 `kfree` 旧页；
- `uvmunmap -> kfree` 每个叶子映射只减一次；
- 计数归零后才填充 `0x5a` 并归还位图。

进程退出时 `uvmfree` 先拆用户叶映射，再释放页表页。`kref_selftest`
在启动阶段验证分配、三路共享、提前释放不污染页面和最终回收。

`cow_alloc` 另用 `cow_lock` 串行化“检查旧页计数 -> 分配新页 -> 更新 PTE
-> 递减旧页”的完整流程；锁序固定为 `cow_lock -> kmem_lock`。修改 PTE 后
必须执行 `sfence_vma()`，防止当前 hart 命中旧翻译。

## 3. 写缺页、copyout 与终止语义

`usertrap` 把 `scause==15`（store/AMO page fault）交给 `cow_alloc`。
合法 COW 页分离后返回用户态；未映射页、非用户页、只读代码页等失败时只设置
`p->killed=1`，由同一函数末尾的 Lab4 检查执行 `proc_exit(-1)`，不会破坏
调度器状态。

`copyout` 是内核写用户页的主要路径。遇到无 `PTE_W` 但带 `PTE_COW` 的页，
先调用 `cow_alloc`，再重新取 PTE 并复制数据，从而保证 `wait` 写退出码、
`exec` 写参数栈等路径同样遵守 COW。

Lab4 的异步调度可能在父进程从 `wait` 醒来前就让子进程退出。若父进程此时才写
状态页，旧页计数已降为 1，会走原地恢复而不是物理复制，导致课程
`cowpair` 的两页复制断言变成 1 次。`sys_wait` 因此在睡眠前对有效状态地址
执行一次 `cow_alloc` 预分离；它只提前完成页分离，不写用户数据，失败时仍由
后续 `copyout` 报告错误。

## 4. exec 用户栈布局

`sys_exec` 从旧地址空间读取 `char **uargv`，用一页内核临时缓冲区保存最多
`MAXARG` 个字符串，再调用 `load_program(..., argv, argc)`。新用户栈按以下
步骤装配：

1. 从高地址向低地址放入 `argv[0..argc-1]` 字符串，保留结尾 NUL；
2. 记录每个字符串的新用户虚拟地址到 `ustack[]`，设置 `ustack[argc]=0`；
3. `sp -= (argc+1)*8` 后再做 16 字节向下对齐；
4. 写入指针数组；
5. 设置 `tf->sp=sp`、`tf->a1=sp`、`tf->a0=argc`，成功返回 `argc`，
   避免 `syscall()` 用返回 0 覆盖 `a0`。

`sh` 启动时也使用同样的 `argc/argv` 布局。非法 `argv` 指针在加载前由
`copyin/copyinstr` 拒绝，不会留下半成品地址空间。

## 5. 配额与统计

`struct proc` 的 `cow_copies` 按进程累计真实复制次数，使用原子递增。
达到 `LAB5_COW_BUDGET=193` 时打印一次内核告警，然后继续复制，不杀进程；
原地恢复 `PTE_W` 不计入该值。`pmc(0)` 返回全局累计复制次数，`pmc(1)`
返回当前共享物理页数，均由 `SYS_pmc=26` 提供。

## 6. Lab4 调度器合并

从真实 Lab4 基线合入：

- `spinlock.c/h`、`swtch.S`、每进程 `p->lock` 和 `wait_lock`；
- `scheduler/sched/yield/sleep/wakeup`、`pause`、`kill`；
- 用户态时钟中断抢占、`cpu_ticks`、`last_slice_ticks`；
- `LAB4_POLICY_ID=7` 步长调度，默认票数 `hi=1 / spin=2 / sh=4`；
- `schedstat`、`time_rdtime`、`setickets`；
- `bench`、`alt`、`stride_test` 及其用户态公共代码。

合并时保留 Lab5 的 `kalloc.c`、`vm.c`、`vm.h`、COW 用户测试、升级版
`sh.c` 和 `exec argv` 接口，并在 Lab4 的 `struct proc` 中加入
`cow_copies`；`allocproc/release_proc_locked/fork/exec` 均重置该字段。

## 7. 验证记录

干净构建 `make clean && make -j2` 成功，仅有基线 RWX 链接警告。

| 测试 | 实测结果 |
| :--- | :--- |
| `zerocopy` | `T5-1 copies: 0 -> 1 (delta 1)`，PASS |
| `cowpair` | `T5-2 copies delta = 2 (expect 2)`，PASS |
| `echo hi` | 输出 `hi`，`T5-3 PASS: argc=2 argv[0]=echo aligned` |
| `cowtest` | 六轮 COW、三路共享、非法代码写、193 次配额降级、exec 参数/非法指针全部 PASS，最终 `T5-SELF ALL PASS` |
| Lab4 | `avg_fork_wait=2915`、`PASS: bench`、`concurrent_children=8`、`alt done`、`stride_ratio=76:224`、`PASS: stride` |
| `lottery_test` | `RESULT lottery_ratio = 148:52`、`PASS: lottery` |
| Lab3 回归 | `TEST-1`、`TEST-3a/3b`、`GUARDTEST`、`COPYTEST`、`TEST-4` 全部 PASS |
| 定时器/UART | `spin 0` 启动，随后计数持续递增并正常结束 |

日志：`lab5-course.log`、`lab4-tests.log`、`lottery-check.log`、
`regression-lab5.log`、`spin-check-lab5.log`。

## 8. 现场问题准备

1. **如何区分合法 COW 与非法写？** 依次检查 `va<MAXVA`、PTE 有效、
   `PTE_U`；已可写则直接成功，否则必须带 `PTE_COW`。未映射、非用户页、
   只读代码页均拒绝。
2. **为什么 trampoline/trapframe 不能 COW？** 它们位于固定高地址并在
   页表切换时使用；若 trapframe 只读共享，`uservec` 保存寄存器时会在进入
   内核早期异常，而当时尚无安全栈恢复上下文。
3. **为什么计数必须原子？** 两个核可能同时看到 `refcnt=2` 并各复制一页，
   旧页最终被错误减到 0 提前回收，造成 Use-After-Free 或重复释放。当前用
   `kmem_lock` 原子化计数与位图，用 `cow_lock` 串行化完整分离流程。
4. **为什么等待前预分离状态页？** Lab4 允许子进程先退出，若父进程较晚写
   状态页，旧页计数已为 1，会失去一次应有的物理复制；预分离保持课程语义。

## 9. 已知边界

验收基线为单 hart（`-smp 1`），`entry.S` 只让 hart 0 进入 `main`。
代码保留每 CPU 数据结构和自旋锁，但不声明完整 SMP 能力；跨核 TLB
shootdown 不在本次实验范围内。
