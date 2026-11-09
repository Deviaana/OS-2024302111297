# Lab4 设计笔记 — 进程上下文切换与调度策略

学号：`2024302111297`

个性化参数：`LAB4_POLICY_ID=7`（步长调度，默认票数按进程名分配）、
`LAB2_TICK=2`、`LAB1_STACK_KB=8`、`LAB3_GUARD_PAGES=2`、
`LAB3_MEMCAP_DELTA=1`。步长默认票数为 `hi=1 / spin=2 / sh=4`，其他程序默认 1；
`setickets` 可覆盖默认值。

---

## 1. 从 lab3 同步模型到 lab4 抢占模型

| 项目 | lab3 状态 | lab4 实现 |
|---|---|---|
| 进程表锁 | 无真正的进程锁，`wait/exit` 直接改 `current` | 每进程 `p->lock`；父子关系与 ZOMBIE 检查由 `wait_lock` 保护 |
| 调度上下文 | 静态 `current`，父进程直接切到子进程 | 每 CPU `struct cpu` 保存调度器 `context`、`proc`、`noff`、`intena` |
| 让出时机 | 只在 `wait` / 控制台轮询中同步交接 | `yield`、`sleep`、`exit` 统一经 `sched -> swtch`；用户态时钟中断可抢占 |
| 调度顺序 | 固定扫描进程表 | 最小 `pass` 的步长调度，同 `pass` 用轮转游标消歧 |
| 休眠唤醒 | 控制台忙等 / `wait` 手工改状态 | `sleep(chan, lk)` / `wakeup(chan)`，检查与入睡原子化 |
| 统计 | 无 | `cpu_ticks`、`last_slice_ticks`、`tickets`、`pass` |

`main()` 不再在 `userinit()` 后空转，而是由 `userinit()` 建立首个 `sh` 进程，
随后进入 `scheduler()`。`myproc()` 从 `mycpu()->proc` 取得当前进程。

---

## 2. `swtch`、`struct context` 与两种现场

`swtch(struct context *old, struct context *new)` 的两个参数位于 `a0`、`a1`。
它保存并恢复 14 个 callee-saved 寄存器：

```text
ra, sp, s0..s11
```

对应偏移依次为 `0, 8, 16, ..., 104`。没有显式保存 PC：

- `ra` 保存的是 `swtch` 的返回地址，也就是调用点之后的下一条指令；
- 保存 `ra` 后，恢复 `ra` 再执行 `ret`，CPU 就回到被换入执行流的调用点之后；
- 对从未运行的新进程，`allocproc()` 把 `context.ra` 设为 `forkret`，
  `context.sp` 设为本进程内核栈顶 `kstack + PGSIZE`。第一次被调度时，
  `swtch` 直接“返回”到 `forkret`。

`forkret()` 第一件事是释放调度器在选中该进程时持有的 `p->lock`，随后调用
`usertrapret()` 进入用户态。若这里不释放，第一次 `fork` 后就会锁重入或死锁。

调用者保存寄存器不需要 `swtch` 处理：C 调用约定允许被调用函数任意改写它们，
所以 `swtch` 的调用者若还要使用这些值，必须已经在自己的栈帧中保存。完整保存
31 个通用寄存器虽然可行，但会扩大切换开销，也没有必要：内核执行流之间的 ABI
只要求 callee-saved 寄存器跨调用保持。

`trapframe` 与 `context` 分工不同：

- `trapframe` 由 `trampoline.S` 保存用户态全部通用寄存器，服务于 U/S 特权级切换；
- `context` 由 `swtch.S` 保存内核执行流寄存器，服务于内核栈之间的调度切换。

---

## 3. 调度器栈与进程切换路径

从用户态进程 A 切到进程 B：

1. A 在用户态被时钟中断，`uservec` 把 31 个用户寄存器写入 A 的 `trapframe`，
   切换到内核页表和 A 的内核栈；
2. `usertrap()` 计数 tick，调用 `yield()`；
3. `yield()` 在 A 的 `p->lock` 下把状态改为 `RUNNABLE`，调用 `sched()`；
4. `sched()` 检查四条不变式，执行 `swtch(&A->context, &cpu->context)`；
5. 控制流进入 CPU 的调度器栈；`scheduler()` 释放 A 的锁，选择 B；
6. 选中 B 时持有 B 的 `p->lock`，调用 `swtch(&cpu->context, &B->context)`；
7. 若 B 以前运行过，从 B 的 `sched()` 返回；若是首次运行，从 `forkret` 开始；
8. B 最终经 `usertrapret()` 与 `userret` 恢复用户寄存器并 `sret`。

调度器不能直接在 A 的内核栈上完成选择：A 可能正在 `exit()`，之后它的内核栈、
页表和 trapframe 会被回收；调度器自己的栈不属于任何用户进程，必须独立存在。

---

## 4. 进程状态、锁与不变式

主状态迁移：

```text
UNUSED --allocproc--> USED --fork 完成--> RUNNABLE
RUNNABLE --scheduler 选中--> RUNNING
RUNNING --yield/时间片--> RUNNABLE
RUNNING --sleep--> SLEEPING --wakeup--> RUNNABLE
RUNNING --exit/异常--> ZOMBIE --wait 回收--> UNUSED
```

锁规则：

- `p->lock` 保护该进程的状态、`chan`、统计字段与上下文交接；
- `wait_lock` 保护父子关系、ZOMBIE 检查和 `wait/exit` 的条件判断；
- 固定锁序为 `wait_lock -> p->lock`；
- `sched()` 进入时必须满足：
  1. 当前进程持有自己的 `p->lock`；
  2. 除该锁外不再持有其他自旋锁（`c->noff == 1`）；
  3. 状态已经不是 `RUNNING`；
  4. 中断关闭。

`p->lock` 必须跨越 `swtch` 交接：如果进程把状态改为 `RUNNABLE` 后提前释放锁，
其他执行流可能在它真正离开内核栈之前选中它，两个执行流会共用同一内核栈。
调度器选中进程时获得锁，进程从 `sched()`/`forkret` 返回后再释放，从而把
“改状态 + 切内核栈”变成一个不可分割的临界区。

---

## 5. `sleep/wakeup` 与丢失唤醒

`sleep(chan, lk)` 的锁序是：

```text
调用者持有 lk
  -> acquire(p->lock)
  -> release(lk)
  -> p->chan = chan; p->state = SLEEPING
  -> sched()
  -> 醒来后 p->chan = 0
  -> release(p->lock)
  -> acquire(lk)
  -> 返回
```

条件检查与“进入 SLEEPING”必须相对 `wakeup` 原子。反例交错：

```text
A: 检查条件不满足，准备休眠
B: 生产资源，扫描进程表时 A 仍不是 SLEEPING，唤醒落空
A: 现在才进入 SLEEPING
=> 没有后续事件再唤醒 A，永久阻塞
```

正确实现中，`wakeup` 也要获得目标进程的 `p->lock`；而 `sleep` 在释放条件锁
之前先拿到 `p->lock`。于是 `wakeup` 的扫描与 A 的入睡不可能插入上述窗口。
`wakeup` 跳过当前进程，避免对自己已持有的 `p->lock` 再次加锁。

`wait/exit` 同样遵守 `wait_lock -> p->lock`。`wait` 在 `wait_lock` 下扫描子进程，
没有 ZOMBIE 时调用 `sleep(p, &wait_lock)`；`exit` 在同样顺序下把自己设为
ZOMBIE 并唤醒父进程。父进程被唤醒后重新检查条件，因此不会丢失退出通知。

控制台也改为同一机制：UART 中断在 `conslock` 下把字符放入环形缓冲区，条件满足
后执行 `wakeup(&buf)`；`consoleread` 在空缓冲区上调用 `sleep(&buf, &conslock)`。

---

## 6. 时钟、抢占与 tick 口径

QEMU virt 的 `time` 为 10 MHz，`TIMER_INTERVAL=1000000`，因此一次时钟中断约为
0.1 秒。本实现采用“`LAB2_TICK` 表示一个时间片包含的中断次数”的口径：
每次用户态时钟中断把 `last_slice_ticks` 加 1，达到 `LAB2_TICK=2` 时调用
`yield()`。即一次用户态时间片约为 2 个 tick / 0.2 秒。

只在 `usertrap()` 的用户态时钟分支抢占；内核态被时钟中断打断时只更新全局
tick，不做上下文切换，因此不需要处理 `kerneltrap` 中 `sepc/sstatus` 被其他
进程改写的复杂性。调度器空闲时开中断并执行 `wfi`，由时钟或 UART 中断唤醒。

全局 tick 及 `tickslock` 用于 `pause(n)`：`sys_pause` 记录起始 tick，循环
`sleep(&ticks, &tickslock)`，直到经过 n 个 tick。`kill` 只设置 `killed` 标志，
把 SLEEPING 目标改为 RUNNABLE；资源仍由目标进程自己退出后交给父进程回收。

---

## 7. ID 7 步长调度

策略元数据直接扩展在 `struct proc` 中：

```text
tickets            当前票数
tickets_override   是否被 setickets 覆盖
pass               步长调度虚拟通过值
cpu_ticks          累计用户态 tick
last_slice_ticks   本次连续运行 tick
```

默认票数由程序名决定：`hi=1`、`spin=2`、`sh=4`，其他程序为 1。`exec` 装载新
程序时恢复该程序的默认票数；`fork` 的子进程继承父进程票数，随后可在新程序中由
`setickets` 覆盖。测试程序正是通过 `setickets(12)` 与 `setickets(36)` 设置 1:3。

每次选中进程时：

```text
stride = 1,000,000 / tickets
选择 RUNNABLE 中 pass 最小者
pass += stride
last_slice_ticks = 0
```

相同 `pass` 时从 `(rr_idx + 1) % NPROC` 开始扫描，避免低槽位进程独占 CPU。
`stride_virtual` 记录最近一次分发时的最小 `pass`；进程休眠醒来或新进程加入时，
若 `pass < stride_virtual` 就抬升到 `stride_virtual`，避免长期休眠后一次性补跑
大量 CPU，破坏份额与窗口公平性。

`setickets(n)` 在 `n < 1` 时返回 -1，否则更新 `tickets` 并设置覆盖标志。

---

## 8. 观测系统调用

- `schedstat(pid, &cpu, &slice)`：扫描进程表并持目标 `p->lock` 读取字段；
  查 `pid=-1` 表示自身；UNUSED/ZOMBIE 返回 -1；释放锁后再 `copyout`。
- `time_rdtime()`：返回 `r_time()` 的硬件计数器值，不是 tick。
- `setickets(n)`：设置当前进程票数，`n < 1` 返回 -1。

`cpu_ticks` 只在 `usertrap()` 的用户态时钟分支累加；内核态被时钟中断打断不统计，
因此测试程序的纯计算负载与观测值一致。调度器分发进程时清零 `last_slice_ticks`。

---

## 9. 验证记录

干净构建：

```bash
make clean && make -j2
```

构建成功，仅保留 lab3 既有的 RWX 链接警告。完整自动回归命令：

```bash
python3 support/inject_uart.py \
    --tree . --script support/lab4-tests.txt --log lab4-test.log
```

实测关键输出：

```text
TEST-1 PASS: unknown syscalls all return -1
TEST-3a PASS: wild write killed (status=-1)
TEST-3b PASS: beyond-sbrk access killed (status=-1)
GUARDTEST PASS: stack guard write killed (status=-1)
COPYTEST PASS: cross-page copyin completed
TEST-4 PASS: OOM returned error after 2031 chunks; existing alloc intact; kernel alive
RESULT avg_fork_wait = 10306
RESULT concurrent_children = 8
PASS: bench
alt done
RESULT stride_ratio = 76:224
PASS: stride
```

`stride_test` 只使用 1:3 票数，实测比例为 `224/76 = 2.947`，落在要求的
2.4–3.6 区间；6 个窗口内两个进程都获得 CPU。`alt` 输出出现 A/B 交错，例如
`AABBBAAAABBBAAAABBBBAABB`，证明用户态时钟抢占生效。另将 `bench` 连续运行
4 次，4 次均输出 `PASS: bench`，未出现丢失唤醒或进程回收失败。

---

## 10. 思考题简答

1. `swtch` 不显式保存 PC，因为 `ra` 已保存调用点的返回地址；恢复 `ra` 后 `ret`
   就回到被换入执行流。它只保存 callee-saved 的 14 个寄存器，caller-saved 由
   各调用者按 ABI 自行保存。完整保存 31 个寄存器可行，但会增加保存/恢复开销和
   `struct context` 大小，且违反最小必要现场原则。
2. A 用户态 -> `uservec`/trapframe -> A 内核栈 `usertrap` -> `sched`/`swtch`
   -> 调度器栈 `scheduler` -> `swtch` -> B 内核栈 `sched`/`forkret`
   -> `usertrapret`/`userret` -> B 用户态。
3. 若 `sleep` 先释放 `lk` 再获取 `p->lock`：A 检查条件失败并释放 `lk`；B 获得
   `lk`、生产资源并 `wakeup`，此时 A 仍未 SLEEPING，唤醒落空；A 随后获取
   `p->lock` 并进入 SLEEPING，从此无人唤醒。先获取 `p->lock` 再释放 `lk` 可以
   消除该窗口。

---

## 11. 当前边界

- 验收基线为单核；`entry.S` 仍未启动副核，因此不声明双核挑战完成；
- 内核态不抢占，只使用用户态时钟中断返回路径；
- 每个进程仍使用一页内核栈；
- `exec` 继续沿用 lab2/lab3 的“忽略 argv、从内嵌程序表加载”接口；
- `sbrk` 仍为 EAGER，COW/LAZY 属于后续 lab5。
