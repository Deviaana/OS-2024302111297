# Lab5 设计笔记 — 写时复制 fork、引用计数与 exec 参数栈

学号：`2024302111297`

个性化参数：`LAB5_REFCNT_SCHEME=1`（全局物理页数组）、`LAB5_COW_BUDGET=193`、`LAB5_KILL_SEMANTICS=1`（完成当前复制后退出）。

本实验在 lab3/lab4 的位图分配器、SV39 页表、`fork/exec/wait/exit` 骨架上完成两项核心改造：把 `fork` 的用户页复制改成写时复制；把 `exec` 的新用户栈装配成符合 RISC-V ABI 的 `argc/argv` 布局。所有物理页的权威引用计数集中放在 `kernel/kalloc.c` 的 `refcnt[]`，并由分配器锁保护。

## 1. COW 三张状态快照

以一块可写数据页 `PA_0` 为例，`refcnt` 表示该物理页被多少张用户页表映射。

### 1.1 fork 刚完成

```text
父 PTE: PA_0, PTE_W=0, PTE_COW=1 ┐
                                 ├──> PA_0, refcnt=2
子 PTE: PA_0, PTE_W=0, PTE_COW=1 ┘
```

`uvmcopy` 不改写物理页内容，只同步清除父子双方 PTE 的 `PTE_W`、设置 `PTE_COW`，再对 `PA_0` 执行一次 `kref_inc`。只读代码段仍按原 `R+X+U` 权限共享，不加 `PTE_COW`，以便非法写代码段时能被准确拒绝。

### 1.2 子进程首次写入

```text
父 PTE: PA_0, PTE_W=0, PTE_COW=1 ──> PA_0, refcnt=1
子 PTE: PA_1, PTE_W=1, PTE_COW=0 ──> PA_1, refcnt=1
```

子进程写缺页进入 `cow_alloc`。旧页 `refcnt=2`，因此分配 `PA_1`、`memmove` 一整页、更新子 PTE，再 `kfree(PA_0)` 把旧页计数减到 1。`pmc(0)` 只在这条真实复制路径递增。

### 1.3 父进程随后写入

```text
父 PTE: PA_0, PTE_W=1, PTE_COW=0 ──> PA_0, refcnt=1
子 PTE: PA_1, PTE_W=1, PTE_COW=0 ──> PA_1, refcnt=1
```

父进程再次触发写缺页时，`cow_alloc` 发现 `PA_0` 的 `refcnt==1`，说明其他共享者已经分离或退出。此时不调用 `kalloc`、不复制，只原地清除 `PTE_COW`、恢复 `PTE_W` 并刷新 TLB，因此不增加 `pmc(0)`。最终仍是两块物理页，各自由一个进程独占。

## 2. 引用计数方案与四个关键场景

### 2.1 三种方案对比

| 方案 | 元数据开销 | 查询/更新复杂度 | 主要约束 |
| :--- | :--- | :--- | :--- |
| 0：PTE 保留位 | 每个叶子 PTE 使用 bit 8/9，无额外数组 | PTE 查找 O(1)，但只能表示 0..3 路共享 | SV39 只有 2 个 RSW 位；`kfree(pa)` 没有页表指针，无法从物理地址反推出所有 PTE，仍需全局数组配合 |
| 1：全局物理页数组 | `uint16[32768]`，约 64 KiB | `kref_get/kref_inc/kfree` 均为 O(1) | 必须与位图共用同一把锁；本实验采用此方案 |
| 2：页表反向扫描 | 不占常驻计数数组 | `kfree(pa)` 需遍历 64 个进程页表，最坏 O(NPROC·页数) | 在 `p->lock/wait_lock` 路径中容易形成锁序死锁；页表页和内核栈页本身不作为用户叶子 PTE，无法靠扫描回收 |

本工程的 `refcnt` 索引为 `(pa - KERNBASE) / PGSIZE`，初始化、递增、递减和空闲位图修改都在 `kmem_lock()`/`kmem_unlock()` 的同一临界区内完成。因此不存在“位图已回收但计数未更新”或反过来的中间窗口。

### 2.2 四个关键场景

| 场景 | 代码位置 | 计数操作 |
| :--- | :--- | :--- |
| 分配新物理页 | `kalloc()` | 成功从位图取页后置 `refcnt=1` |
| fork 建立共享映射 | `uvmcopy()` | 每个成功映射的用户页 `kref_inc(pa)`；失败回滚时由 `uvmunmap` 递减 |
| 写缺页分离旧页 | `cow_alloc()` | 新页由 `kalloc` 得到 1；更新 PTE 后 `kfree(old_pa)` 递减旧页 |
| 进程退出解除映射 | `uvmunmap()` -> `kfree()` | 每拆除一个叶子映射递减一次；仅在计数归 0 时填 `0x5a` 并归还位图 |

`kinit` 在把页放入空闲池前把 `refcnt` 预置为 1，因此初始化本身可以复用严格的 `kfree` 语义。`kref_selftest` 验证了 1->3->1->0 的计数链，并确认计数未归零时页面不会被毒化。

## 3. 写缺页、copyout 与终止语义

`usertrap` 对 `scause==15` 调用 `cow_alloc(p->pagetable, r_stval())`。合法页必须同时满足：地址小于 `MAXVA`、叶子 PTE 有效、带 `PTE_U`、且处于 `PTE_COW` 状态。若 PTE 已经是可写页则直接返回成功；若无效、非用户页、未映射或只读代码段，则返回 -1，进程以 -1 终止，内核继续运行。

本学号的 `LAB5_KILL_SEMANTICS=1`：即使进程已经被标记 `killed`，也先尝试完成当前 `cow_alloc`，随后在 `usertrap` 末尾统一退出。这样保证“当前缺页已经进入分离流程”时不会留下半更新状态。

本基线没有单独的 `uvm_check_range` 函数，所有 `read/wait/exit/exec` 的内核写入都汇入 `copyout`。`copyout` 在目标页 `PTE_W==0` 且带 `PTE_COW` 时先调用 `cow_alloc`，重新取 PTE 后再写入；否则返回 -1。这修复了 shell 的 `.bss` 缓冲区在 fork 后变为 COW 时 `read` 无限失败的问题。

`cow_alloc` 用一把 `cow_lock` 串行化“检查旧页计数 -> 分配新页 -> 更新 PTE -> 递减旧页”。若双核同时看到旧页 `refcnt==2`，没有该锁时可能各复制一次，使 `cowpair` 的计数变成 3/4；串行化后只有先写方复制，后写方看到 1 并原地恢复。修改 PTE 后必须执行 `sfence_vma()`，否则当前 hart 的 TLB 仍可能命中旧的只读翻译。

## 4. exec 用户栈布局

`sys_exec` 从旧地址空间读取用户 `char **uargv`，用一页内核临时缓冲区保存最多 `MAXARG` 个参数字符串。`load_program` 在新地址空间中依次完成：

1. 对 `argv[0..argc-1]` 从高地址向低地址压入字符串，每个字符串末尾保留 NUL，并把 `sp` 向下对齐到 16 字节；
2. 记录每个字符串的新用户虚拟地址到 `ustack[]`，并设置 `ustack[argc]=0`；
3. `sp -= (argc+1)*8` 后再次执行 `sp &= ~0xf`，把“8 字节对齐填充”留在字符串区与指针数组之间；
4. 写入 `argv[0..argc]` 指针数组；
5. 设置 `tf->sp=sp`、`tf->a1=sp`、`tf->a0=argc`，并让 `load_program/sys_exec` 成功返回 `argc`，避免 `syscall()` 用返回 0 覆盖 `a0`。

例如执行 `exec("echo", ["echo","hi",0])` 时，最终 `argv` 指针本身 16 字节对齐，`argv[2]` 为 NULL，`argv[0]` 指向字符串 `echo`，`argv[1]` 指向 `hi`。`echo hi` 的实测输出为：

```text
hi
T5-3 PASS: argc=2 argv[0]=echo aligned
```

## 5. 配额管理

`struct proc` 中的 `cow_copies` 按进程累计真实复制次数。达到 `LAB5_COW_BUDGET=193` 时打印一次内核告警，然后继续完成复制，而不是杀死进程或让全局状态失效。这样 `bench` 一类多轮 fork/wait 回归不会因为累计配额耗尽而中断。原地恢复 `PTE_W` 不计入该计数，也不计入 `pmc(0)`。

## 6. 验证证据

构建：`make clean && make -j2` 通过，仅有基线内核 RWX 段的链接警告。

| 测试 | 结果 |
| :--- | :--- |
| `zerocopy` | `T5-1 copies: 0 -> 1 (delta 1)`，`T5-1 PASS` |
| `cowpair` | `T5-2 copies delta = 2 (expect 2)`，`T5-2 PASS` |
| `echo hi` | 输出 `hi`，`T5-3 PASS: argc=2 argv[0]=echo aligned` |
| `cowtest` | 六轮 COW、三路共享、非法代码写、193 次配额降级、exec 参数/非法指针全部 PASS，最终 `T5-SELF ALL PASS` |
| lab3 回归 | `badecall`、`badaccess`、`guardtest`、`copytest`、`memcap` 全部 PASS |
| 串口/定时器回归 | `spin 0` 与后续非零计数均 PASS |

日志文件：`lab5-course.log`、`regression-lab5.log`、`spin-check-lab5.log`。

## 7. 现场问题准备

1. **scause==15 如何区分合法 COW 与非法写？** 依次判断 `va<MAXVA`、PTE 有效、`PTE_U`、已可写则直接成功、否则必须 `PTE_COW`；未映射、非用户页、只读代码段均拒绝。
2. **trampoline/trapframe 为什么不能 COW？** 它们有固定的高虚拟地址且跨页表切换必须保持当前 CPU 可执行/可写。若 trapframe 被设为只读共享，`uservec` 在下一次用户陷入时写 `sscratch` 保存寄存器会触发异常，而此时内核还没有安全栈和页表上下文，系统会在跳板汇编早期崩溃。
3. **双核为何必须锁住计数？** 例如两个核同时读到 2，各自分配新页并递减，旧页最终被减到 0 提前回收，另一进程 PTE 仍指向它；后续写会形成 Use-After-Free，页面再次分配后还会出现数据串扰或双重释放。本实现用 `kmem.lock` 原子化计数与位图，用 `cow_lock` 串行化完整分离流程。

## 8. 已知边界

基础工程是单 hart 启动模型，`entry.S` 只让 hart 0 进入 `main`。本实验所需的引用计数锁和 COW 分离锁按共享内存原子操作实现，但完整的 SMP 调度、跨核 TLB shootdown 和 lab4 调度器不在本基线内。
