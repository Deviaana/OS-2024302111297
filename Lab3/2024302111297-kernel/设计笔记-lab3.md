# Lab3 设计笔记 — SV39 三级页表与物理内存管理

学号：`2024302111297`

个性化参数：`LAB3_ALLOC_ORDER=1`（降序分配）、`LAB3_GUARD_PAGES=2`、`LAB3_MEMCAP_DELTA=1`、`LAB1_STACK_KB=8`、`LAB2_BUF_SIZE=64`、`LAB2_BUF_SEMANTICS=1`、`LAB1_BANNER_PROTOCOL=1`。

本实验在 lab2 的陷入、系统调用、控制台和进程框架上引入 RISC-V SV39 分页：物理内存由位图分配器托管；内核和每个用户进程拥有独立的页表；用户指针全部经受控拷贝访问；`fork`、`exec`、`sbrk`、`wait/exit` 都按页表化语义重写。本笔记按“设计决策、资源所有权、失败回滚、验证证据”的顺序记录实现。

---

## 1. SV39 地址翻译与基本约定

### 1.1 虚拟地址格式

SV39 的有效虚拟地址为 39 位，划分为：

```text
 38            30 29            21 20            12 11             0
+----------------+----------------+----------------+----------------+
|  VPN[2] (9bit) |  VPN[1] (9bit) |  VPN[0] (9bit) | offset (12bit) |
+----------------+----------------+----------------+----------------+
```

`kernel/riscv.h` 中的 `PX(level, va)` 用 `(va >> (12 + 9*level)) & 0x1ff` 提取三级索引。`MAXVA = 1 << 38 = 0x4000000000`，因此本实验最高可用的 4 KiB 页是 `0x3ffffff000`。`satp` 的 MODE 字段为 8，根页表物理页号放在低 44 位，`MAKE_SATP(pagetable)` 完成 `8 << 60 | (pa >> 12)` 的拼接。

### 1.2 页表项

每个页表占 4 KiB，含 512 个 8 字节 PTE。PTE 的 PPN 左移 10 位，低 10 位为标志位。本项目使用：

- `PTE_V`：有效；
- `PTE_R`/`PTE_W`/`PTE_X`：读、写、执行；
- `PTE_U`：允许 U 模式访问。

中间级 PTE 的 R/W/X 必须全为 0，硬件据此识别“下一级页表”；叶子 PTE 至少有一个 R/W/X 位。`walk()` 在下降过程中显式检查中间级 PTE 是否被误写成叶子，避免把物理页当作页表继续遍历。

### 1.3 页表切换与 TLB

所有会改变地址翻译的操作都调用 `sfence_vma()`：

1. `kvminithart()` 写 `satp` 前后各刷新一次；
2. `mappages()` 成功建立一段映射后；
3. `mappages()` 回滚完成之后；
4. `uvmunmap()` 拆除映射之后；
5. `uservec` 切到内核页表前后、`userret` 切回用户页表前后。

如果不刷新，TLB 可能继续命中旧的虚拟页到物理页映射：轻则用户进程看到旧的代码/数据，重则在页已经被释放并重新分配后仍能访问它。本实验为单核，`sfence.vma zero, zero` 足以刷新当前 hart；多核时还需要通过核间中断让其他 hart 执行本地刷新，这一点留作思考题。

---

## 2. 物理页分配器

### 2.1 托管范围与配额

`kinit()` 在启用分页前运行，直接使用物理地址：

```text
managed_start = PGROUNDUP(kernel_end)
managed_pages = (PHYSTOP - managed_start) / PGSIZE
alloc_limit   = managed_pages - LAB3_MEMCAP_DELTA
```

`kernel_end` 由 `kernel.ld` 导出，表示内核镜像和静态 BSS 的末尾。把它向上取整到页边界，既不会把链接器最后一部分页交给分配器，也保证后续 `kfree()` 的页对齐检查成立。`PHYSTOP = KERNBASE + 128 MiB`，与 QEMU `virt` 的 128 MiB RAM 配置一致。

`LAB3_MEMCAP_DELTA=1`，所以最后一个托管页永久保留，不参与 `kalloc()`。保留页在初始化时就把位图对应位置 1，既不会分配出去，也不会被误释放。

### 2.2 位图 vs. 有序空闲链表

实现采用位图：每个 4 KiB 物理页对应 1 bit，128 MiB / 4 KiB = 32768 页，位图仅 32768 bit = 4 KiB。位图作为内核 BSS 的静态数组存在，不额外占用堆。

| 方案 | 单页分配 | 单页释放 | 元数据空间 | memcap 批量释放 |
|---|---:|---:|---:|---:|
| 地址有序单向链表 | O(1)（取头） | O(n)（按地址插入） | 每页至少一个指针，约 8 B/页 | 最坏 O(n²)，数万页时可能达到数分钟 |
| 位图（本实现） | O(P) 最坏扫描，P 为页数 | O(1) | 1 bit/页，总计约 4 KiB | O(n)，每页常数时间 |

在 `memcap` 场景中，释放路径的复杂度比分配路径更关键：测试会先分配数千个页，再在进程退出时批量释放。位图让 `kfree()` 只做“检查、填垃圾、清位、计数”四步，不会在释放时维护有序链表。

### 2.3 学号参数：降序分配

`LAB3_ALLOC_ORDER=1` 要求降序分配。`kalloc()` 从 `alloc_limit - 1` 向下扫描，找到第一个空闲位后：

1. 置位；
2. `free_pages--`；
3. 用 `memset(page, 0, PGSIZE)` 清零；
4. 返回该页的物理地址指针。

降序是本学号的个性化语义，不是优化选项。测试时前几次 `kalloc()` 返回的地址应逐步下降；释放后再次分配仍应优先选择当前最高的空闲页。

### 2.4 清零、垃圾填充与错误处理

- `kalloc()` 清零：避免上一个进程的页内容泄漏给新进程，也让新页表页天然为零。这一点对页表尤其重要：未清零的中间 PTE 可能被硬件误认为有效映射。
- `kfree()` 填 `0x5a`：让悬挂指针在调试时呈现明显的 `0x5a5a...` 数据，而不是悄悄读到旧内容；同时保留“页面已释放”的现场。
- `kfree()` 会检查指针对齐、是否在托管范围内、是否属于保留配额、是否重复释放。任何非法释放都 `panic`，以便尽早暴露所有权错误。
- `kalloc()` 在配额耗尽时返回 `NULL`，不 `panic`。调用者负责把失败向上传递并回滚。
- `kfreepages()` 返回当前空闲页数，可用于临时自检“分配/释放守恒”。

### 2.5 配额耗尽的传播链

`kalloc()` 返回 `NULL` 后，各层的约定是：

| 调用点 | 失败行为 |
|---|---|
| `walk(..., alloc=1)` | 返回 `NULL`，不建立半成品叶子 PTE |
| `mappages()` | 拆除本次已建立的 PTE，释放本次新建的空中间页表，返回 `-1`；调用方的叶子物理页不被释放 |
| `uvmcreate()` | 返回 `NULL` |
| `load_program()` | 释放新页表、陷入帧和已映射页，返回 `-1`，旧地址空间不受影响 |
| `uvmcopy()` | 释放新页表中已复制的页，返回 `-1` |
| `sys_sbrk()` | 拆除本次扩展已建立的映射，返回 `(uint64)-1` |
| `sys_fork()` | 回收子进程槽位，返回 `-1` |

`memcap` 的实测结果是：`sbrk` 在 2031 个 64 KiB 块后返回错误，已分配块仍可读写，内核和 shell 保持运行。

---

## 3. walk、mappages 与回滚不变式

### 3.1 walk 的返回值

`walk(pagetable, va, alloc)` 从根页表开始，逐级取 `PX(2,va)`、`PX(1,va)`、`PX(0,va)`：

- 中间 PTE 有效且不是叶子：取出下一级页表物理地址继续；
- 中间 PTE 缺失且 `alloc=1`：`uvmcreate()` 分配并清零一个页表页，写入 `PA2PTE(child) | PTE_V`；
- 中间 PTE 缺失且 `alloc=0`：返回 `NULL`；
- 遇到 `va >= MAXVA`：返回 `NULL`。

最终返回的是**叶子 PTE 的指针**，不是物理地址。调用者需要读/改权限位、清 `PTE_U`、判断有效位，或者把 PTE 置 0，因此必须拿到 PTE 本身。`walkaddr()` 在此基础上检查 `PTE_V`、`PTE_U` 并返回物理地址，供内核短路径使用。

### 3.2 mappages 的前置条件与重复映射

`mappages(pagetable, va, size, pa, perm)` 要求 `va`、`pa`、`size` 都按 4 KiB 对齐且 `size > 0`，并检查 `va + size` 不超过 `MAXVA`。映射逐页推进，每页通过 `walk(..., 1)` 取叶子 PTE。

对已经存在有效映射的虚拟地址，本实现返回 `-1`，不覆盖旧映射。覆盖会静默丢失旧物理页的所有权，也会让调用者误以为新映射成功；返回错误可以让调用者保留旧状态并自行决定是否先 `uvmunmap()`。

### 3.3 回滚一致性的四条不变式

1. **叶子物理页归调用方所有。** `mappages()` 只安装 PTE，不 `kfree()` 调用方传入的 `pa`。失败时已安装的 PTE 用 `uvmunmap(..., do_free=0)` 拆除，物理页仍由调用方释放。
2. **中间页表页由 mappages 在回滚时清理。** `walk(..., 1)` 为本次映射新建的中间页表，如果回滚后变为空表，由 `free_empty_tables()` 释放；调用前已经存在且仍有其他有效项的页表不会被释放。
3. **只回滚本次调用碰过的范围。** 回滚从 `va` 开始，到失败页 `a` 为止；不会拆除调用前已经存在的其他映射。
4. **根页表不被 mappages 释放。** 根页表由 `uvmcreate()` 的调用者持有，最终由 `uvmfree()`/`proc_freepagetable()` 处理。

具体流程是：

```text
成功映射 [va, a) 的 N 页
第 N+1 页 walk 失败（kalloc 无页或遇到重复映射）
  -> uvmunmap(pagetable, va, N, do_free=0)   只摘 PTE，保留调用方物理页
  -> free_empty_tables(pagetable, 2, va, PGROUNDUP(a+PGSIZE))
      只释放范围内已经变空的中间页表
  -> sfence_vma()
  -> return -1
```

`free_empty_tables()` 会沿着仍然有效的中间 PTE 递归，只在 `table_empty(child)` 为真时释放子表。它不会碰叶子 PTE，也不会释放仍有其他有效项的共享页表。这样既清理了失败路径新建的页表页，又不会破坏调用前已有的映射。

### 3.4 调用方的配套回滚

`map_image_pages()` 是逐页调用 `mappages()` 的，因此如果后续页失败，前面成功页的 PTE 仍在页表里。`load_program()` 在开始映射前就把 `rollback_sz` 设为 `PGROUNDUP(info.end)`，失败时统一调用 `proc_freepagetable()`，由它按 `[0, rollback_sz)` 拆除已安装的叶子页。这个顺序很重要：若等到 `map_image_pages()` 成功后才设置 `rollback_sz`，中途 OOM 会以 0 范围释放页表，`freewalk()` 会因发现残留叶子 PTE 而 panic。

---

## 4. 内核页表

### 4.1 映射清单

`kvmmake()` 在启用分页前构造内核根页表，映射清单如下：

| 区域 | 虚拟地址 = 物理地址 | 大小 | 权限 | 用途 |
|---|---|---:|---|---|
| UART0 | `0x10000000` | 4 KiB | R+W | `console.c` 直接读写 UART 寄存器 |
| PLIC | `0x0c000000` | 64 MiB | R+W | 中断使能、优先级、claim/complete |
| 内核镜像与全部 RAM | `0x80000000` | `PHYSTOP - KERNBASE` | R+W+X | 内核代码、数据、BSS、位图、所有页表页和用户物理页 |
| 跳板页 | `TRAMPOLINE` | 4 KiB | R+X | 与所有用户页表保持同址映射 |
| 进程内核栈 | `KSTACK(p)` | 每进程 4 KiB | R+W | 每个进程独立的 S 态栈，栈顶结束于 `KSTACK(p)+PGSIZE` |

RAM 的恒等映射必须一直覆盖到 `PHYSTOP`，而不是只覆盖到 `kernel_end`。启用分页后，`kalloc()` 返回的空闲物理页、`uvmcreate()` 分配的页表页、用户数据页都可能位于镜像之外；内核需要直接访问它们的物理地址。如果只映射到镜像末尾，第一次分配页表页或进程数据页就会触发内核缺页。

### 4.2 CLINT 取舍

本内核的时钟来自 lab2 已使用的 `rdtime`/`stimecmp` CSR：`start.c` 打开 `menvcfg.STCE`，`trap.c` 通过 `w_stimecmp()` 设置下一次比较值。内核从不访问 CLINT 的 MMIO 寄存器，因此 `kvmmake()` 不映射 `0x02000000`。这样既减少一条无用的设备映射，也避免把不需要的定时器寄存器暴露给内核页表。

### 4.3 权限约束

增量包提供的 `kernel.ld` 没有导出 `etext`，无法在不修改支持文件的前提下把内核代码段和数据段拆成不同权限。本实现将内核镜像和 RAM 统一映射为 R+W+X，这是说明书明确允许的阶段性约束。代价是内核代码页理论上可写；用户态仍然无法访问这些映射，因为内核 PTE 没有 `PTE_U`。后续如果链接脚本提供 `etext`，可以进一步拆成代码 R+X、数据 R+W 的 W^X 布局。

### 4.4 内核栈

本实现采用 `KSTACK(p) = TRAMPOLINE - (p+1) * 2 * PGSIZE` 的高地址映射：每个进程一页可读写内核栈，栈下方保留一页未映射空洞，避免内核栈溢出直接踩到相邻进程的栈。`proc_mapstacks()` 为 64 个进程各分配一页物理页并建立高地址映射。该布局与 lab0 进程快照图中“内核栈位于跳板页下方”的约定一致。

---

## 5. 用户地址空间与程序装载

### 5.1 布局

用户程序按虚拟地址 0 链接，布局从低到高为：

| 区域 | 起始地址 | 权限 | 说明 |
|---|---|---|---|
| 代码/只读数据 | `0x0` | R+X+U | 从平铺二进制复制；严禁 W |
| 可写数据/BSS | 派生链接脚本页对齐后的 `_data_start` | R+W+U | 初始数据来自二进制，BSS 由清零页提供 |
| guard pages | `PGROUNDUP(image_end)` 起 2 页 | 无 PTE | 不分配物理页，访问即缺页 |
| 用户栈 | `guard_end` 起 1 页 | R+W+U | `sp = stack_top - 16`，向下增长 |
| sbrk 堆 | `PGROUNDUP(stack_top)` 起 | R+W+U | EAGER 扩展，向高地址增长 |
| TRAPFRAME | `TRAMPOLINE - PGSIZE` | R+W，无 U | 每进程一个物理页，保存 31 个用户寄存器和内核入口字段 |
| TRAMPOLINE | `MAXVA - PGSIZE` | R+X，无 U | 所有地址空间共享同一物理代码页 |

以本工程的 `sh` 为例，实际 dump 为：代码页 `0x0 r-xu`、数据页 `0x1000 rw-u`、guard 空洞 `0x2000`/`0x3000`、用户栈 `0x4000 rw-u`、陷入帧 `0x3fffffe000 rw--`、跳板页 `0x3ffffff000 r-x-`。堆从 `0x5000` 开始，初始 `p->sz = 0x5000`。

### 5.2 保护页

保护页不分配、不映射物理页。用户从栈向下越界或向堆上方越界访问时，硬件在地址翻译阶段找不到有效 PTE，产生：

- 取指越界：`scause=12`（instruction page fault）；
- 读越界：`scause=13`（load page fault）；
- 写越界：`scause=15`（store page fault）。

这些异常从用户态进入 `usertrap()`，`usertrap()` 的非系统调用、非中断分支调用 `proc_exit(-1)`，只终止当前进程；父进程在 `wait()` 中得到 `-1` 状态并回收子进程，内核继续运行。保护页不消耗物理页，也不会因为“未映射”被 `fork` 误判为错误：`uvmcopy()` 遇到 `pte == NULL` 或 `PTE_V=0` 的洞时直接 `continue`。

### 5.3 代码段/数据段权限拆分

平铺二进制不携带 ELF 段属性，因此需要三步传递边界：

1. **构建期提取**：`Makefile` 从课程提供的 `user/user.ld` 派生 `user-flat/user.ld`，在 `.data` 前插入 `_text_end`、`ALIGN(4096)` 和 `_data_start`；链接后用 `nm` 读出这三个符号以及 `end`。
2. **传递到内核**：`kernel/userimg.S` 的程序表每项扩展为 5 个 `uint64`（镜像起始、`end`、`text_end`、`data_start`、文件字节数）加程序名；`kernel/userentry.S` 单独保存每个程序的 `main` 入口。
3. **装载时判定**：`map_image_pages()` 把 `[0, PGROUNDUP(text_end))` 映射为 R+X+U，把 `[PGROUNDDOWN(data_start), PGROUNDUP(end))` 映射为 R+W+U；每页从 `info->start` 复制 `min(file_size - va, PGSIZE)` 字节，未复制部分保持清零。

派生链接脚本保证了即使程序很小，`.data` 也从新页开始，不会和代码共享同一个 PTE。代码页没有 W，因此 `badaccess` 的 TEST-3a 写地址 0 会触发 store page fault；如果整镜像映射为 RWX，这个测试就会失败。

### 5.4 陷入帧与跳板

`TRAPFRAME` 和 `TRAMPOLINE` 都在用户页表的高地址，但都不带 `PTE_U`，用户态不可直接访问。`TRAPFRAME` 每进程独立分配，`fork` 时单独复制其内容；`TRAMPOLINE` 是共享的只读代码页，所有用户页表和内核页表映射到同一个虚拟地址。`usertrapret()` 在 C 代码中完成 `sstatus`/`sepc` 设置后，通过高地址函数指针跳到 `userret`，再由它写 `satp`、恢复寄存器并 `sret`。不能在 C 函数中直接写用户 `satp` 后继续顺序执行，因为下一条取指就会落到用户页表中不存在的内核地址。

---

## 6. copyin / copyout 与直接解引用审计

### 6.1 策略

本实现采用“逐页校验 + 页内批量 `memmove`”：

1. 计算 `va0 = PGROUNDDOWN(srcva/dstva)`；
2. 检查 `va0 < MAXVA` 且 `va0 < sz`；
3. `walk(..., 0)` 找到叶子 PTE，要求 `PTE_V`、`PTE_U`，并根据方向要求 `PTE_R`（copyin）或 `PTE_W`（copyout）；
4. 计算本页剩余字节 `n = PGSIZE - (va - va0)`，与剩余长度取较小值；
5. 用 `memmove` 复制这一页内的连续片段，然后推进到下一页。

这样既避免逐字节拷贝带来的函数调用和边界判断开销，也避免“先校验整个区间、再一次性 memcpy”在跨页洞或权限不一致时的漏洞。校验和复制按页交替进行，遇到缺失 `PTE_U`、guard 洞、代码页写、只读页写、超出 `sz` 的地址都会立即返回 `-1`，不会把非法地址当作内核物理地址解引用。

`copyinstr()` 为了寻找 NUL 终止符逐字节调用 `copyin()`；每次仍然经过页表权限检查，最大长度由调用者给出，找不到 NUL 就返回 `-1`。

### 6.2 系统调用审计

| 位置 | lab2 的直接访问 | lab3 处理 |
|---|---|---|
| `sys_write()` | 直接读用户 `buf` | 每次 256 字节 `copyin()` 到内核临时缓冲区，再写 UART |
| `sys_read()` | 直接写用户 `buf` | `consoleread()` 先写内核缓冲区，再 `copyout()` |
| `argstr()` / `sys_exec()` | 直接读用户字符串 | `copyinstr()` + 长度上限 |
| `sys_wait()` | 直接写用户 `status` 指针 | `copyout()` 写 `xstatus` |
| `proc_exit()` | 直接写父进程 `status` 指针 | `copyout()` 写父进程地址空间 |
| `console.c` | 只接收内核 `dst` | 保持内核缓冲区接口，不接触用户指针 |

当前内核中没有遗留的 `*(user_pointer)` 形式直接解引用；所有从 `trapframe->a0..a5` 取得、可能指向用户空间的地址都经过上述受控接口。`copyout()` 还会拒绝 `TRAPFRAME`/`TRAMPOLINE` 这类 `PTE_U=0` 的高地址，防止用户借系统调用改写陷入帧或跳板页。

---

## 7. 进程控制块与地址空间生命周期

### 7.1 struct proc 的演进

在 lab2 的 `state/pid/parent/xstatus` 基础上，本实验新增：

| 字段 | 类型 | 作用 |
|---|---|---|
| `pagetable` | `pagetable_t` | 该进程独立的用户根页表；上下文切换时写入 `satp` |
| `trapframe` | `struct trapframe *` | 每进程一页的陷入帧物理页，固定映射到 `TRAPFRAME` |
| `kstack` | `uint64` | 该进程内核栈的高地址基址，栈顶为 `kstack + PGSIZE` |
| `sz` | `uint64` | 当前用户地址空间上界（堆尾），也是 `copyin/copyout/uvmcopy` 的范围上界 |
| `heap_start` | `uint64` | sbrk 堆的起始地址，`sbrk` 收缩时不得低于它 |
| `stack_top` | `uint64` | 用户栈顶，装载时设置 `sp` |

### 7.2 生命周期

| 阶段 | 维护函数 | 动作 |
|---|---|---|
| 启动 | `proc_mapstacks()` | 为每个槽位分配一页内核栈并建立 `KSTACK(i)` 高地址映射 |
| 创建 | `proc_alloc()` | 分配根页表和陷入帧物理页，初始化状态、pid、sz 等 |
| 首个进程 | `userinit()` | 用 `load_program(sh)` 建立代码、数据、guard、栈、堆、TRAPFRAME、TRAMPOLINE，调用一次 `dump_pagetable()` 后进入用户态 |
| fork | `sys_fork()` | 新建页表和陷入帧，深拷贝父进程 `[0, sz)` 的用户页，映射子进程的 TRAPFRAME/TRAMPOLINE |
| exec | `sys_exec()` | 先完整构建新地址空间，成功后一次性替换旧地址空间 |
| 运行 | `usertrapret()`/`userret` | 通过 `p->pagetable` 生成用户 `satp`，切换页表并恢复用户寄存器 |
| 退出/回收 | `proc_exit()`/`release_proc()` | `proc_freepagetable()` 拆除 TRAMPOLINE、TRAPFRAME 和 `[0, sz)` 的用户页，释放页表页 |

### 7.3 fork 的逐页深拷贝

`uvmcopy(old, new, sz)` 遍历 `[0, sz)` 的每一页：

- `walk(old, va, 0)` 找不到 PTE 或 `PTE_V=0`：这是 guard 空洞或其他合法空洞，直接跳过，不报错；
- `PTE_U=0`：跳过（正常情况下 `[0,sz)` 内不应有内核页）；
- 有效用户页：`kalloc()` 新页，复制整页内容，用原 PTE 的 `R/W/X/U` 权限在子页表中重新映射；
- `kalloc()` 或 `mappages()` 失败：释放子页表中已复制的所有页，返回 `-1`，父进程不受影响。

`trapframe` 和 `trampoline` 不参与 `uvmcopy`：

- 子进程在 `proc_alloc()` 中已经有自己的 `trapframe` 物理页；`*c->trapframe = *p->trapframe` 复制寄存器现场，再把 `a0` 改为 0，表示子进程从 fork 返回 0。它的 `kernel_sp` 必须改成子进程自己的 `kstack + PGSIZE`。
- `trampoline` 是只读代码页，所有进程共享同一物理页；在子页表中重新建立同一虚拟地址的映射即可，不能为每个进程复制一份，否则既浪费内存，也破坏“同址代码”的共享语义。

### 7.4 exec 的资源时序

`sys_exec()` 的顺序是“先建新、后拆旧”：

1. `argstr()` 从用户空间安全复制程序名，并去掉行尾空白；
2. `load_program()` 新建根页表、陷入帧，映射镜像、guard、栈、TRAPFRAME、TRAMPOLINE；
3. 任一步失败：只释放新资源并返回 `-1`，`p->pagetable`、`p->trapframe`、`p->sz` 完全不变，旧程序可以继续执行；
4. 全部成功：`proc_freepagetable()` 释放旧地址空间，然后替换指针和边界；
5. 用当前进程的 `kstack` 重写新 trapframe 的 `kernel_sp`，保证下一次陷入使用正确的内核栈。

`load_program()` 的 `rollback_sz` 必须在 `map_image_pages()` 之前设置，原因已在 §3.4 说明：镜像映射可能在中途 OOM，失败路径仍需要知道已安装叶子的范围。

### 7.5 sbrk（EAGER）

`sys_sbrk(n, which)` 忽略 `which`，统一按 EAGER 语义：

- `n == 0`：返回当前 `p->sz`，不改变映射；
- `n > 0`：从 `PGROUNDUP(oldsz)` 到 `PGROUNDUP(newsz)` 逐页 `kalloc()` + `mappages(R+W+U)`；任何一页失败时，拆除本次已建立的页并返回 `-1`，`p->sz` 保持旧值；
- `n < 0`：要求 `newsz >= heap_start` 且不溢出；把跨越的整页 `uvmunmap(..., do_free=1)`，然后更新 `p->sz`。

扩展范围不能越过 `TRAPFRAME`，因为那是内核专用的陷入帧映射。LAZY 分配不在本实验范围内，完整缺页处理留到 lab5。

### 7.6 wait/exit 与同步进程模型

本实验沿用 lab2 的同步模型：`fork()` 后子进程处于 `RUNNABLE`，父进程执行 `wait()` 时把自身设为 `BLOCKED`，把子进程设为 `RUNNING` 并切换 `current`；子进程 `exit()` 时把状态写回父进程 trapframe 的 `a0` 和用户 `status` 指针，再把父进程恢复为 `RUNNING`。因此 shell 的 `fork/exec/wait` 链可以稳定工作，`badaccess`、`memcap` 的父进程都能回收子进程。

这个模型的限制是：没有时间片轮转，`spin` 运行期间 shell 不会获得 CPU；但 UART 中断、回显和定时器仍然正常工作。真正的抢占式调度、MLFQ、优先级和 sleep/wakeup 属于 lab4 的内容。本笔记不把 lab2 的同步模型说成完整的通用调度器，避免与后续实验的职责混淆。

### 7.7 stvec 的切换位置

增量包提供的 `uservec` 只负责保存现场、切换到内核页表和调用 `usertrap()`，不会重设 `stvec`。因此 `usertrap()` 的第一行必须是：

```c
w_stvec((uint64)kernelvec);
```

否则内核态发生时钟中断时，`stvec` 仍指向用户向量 `uservec`，会以 S 态寄存器去访问用户 trapframe，最终在跳板页附近产生 store fault 并停机。返回用户态前，`usertrapret()` 再把 `stvec` 设为 `TRAMPOLINE + (uservec - trampoline)`。

---

## 8. dump_pagetable 观测

`dump_pagetable()` 按 `dump_pagetable-ABI.md` 输出：

```text
DUMP-PAGETABLE begin
L2 va=0x0 pa=... perm=----
L1 va=0x0 pa=... perm=----
LEAF va=0x0 pa=... perm=r-xu
...
DUMP-PAGETABLE end
```

规则是：非叶子中间项输出 `L2`/`L1`，权限固定为 `----`；叶子项输出 `LEAF`；`perm` 严格按 R/W/X/U 四字符输出，未置位用 `-`。A/D 位属于硬件运行时状态，不放进 `perm` 四位中。遍历按虚拟地址升序，便于与 lab0 快照图逐项对照。

本工程启动时 `sh` 的实际输出为：

```text
DUMP-PAGETABLE begin
L2 va=0x0 pa=0x87f55000 perm=----
L1 va=0x0 pa=0x87f54000 perm=----
LEAF va=0x0 pa=0x87f56000 perm=r-xu
LEAF va=0x1000 pa=0x87f53000 perm=rw-u
LEAF va=0x4000 pa=0x87f52000 perm=rw-u
L2 va=0x3fc0000000 pa=0x87f51000 perm=----
L1 va=0x3fffe00000 pa=0x87f50000 perm=----
LEAF va=0x3fffffe000 pa=0x87f57000 perm=rw--
LEAF va=0x3ffffff000 pa=0x80002000 perm=r-x-
DUMP-PAGETABLE end
```

其中 `0x2000` 和 `0x3000` 两个 guard 页没有叶子项，正好体现“保护页不分配物理页”。`0x0` 为 `r-xu`（代码不可写），`0x1000` 为 `rw-u`（数据可写），`0x4000` 为 `rw-u`（用户栈），高地址陷入帧为 `rw--`（内核可读写，用户不可访问），跳板页为 `r-x-`（共享可执行代码，无 U）。

---

## 9. 自测用例、官方测试与运行记录

以下两组用例在编码实现前确定，分别覆盖“保护页/非法访存被安全终止”和“跨物理页受控拷贝”；实现完成后按同一判据回归并记录输出。

### 9.1 用例 A：guard page / 非法访存拦截

设计：子进程从用户态向未映射地址或 guard page 下方读写，期望触发用户态缺页异常；内核只终止子进程，父进程 `wait()` 返回非零状态，shell 继续接受命令。

实现：`user/badaccess.c`

- TEST-3a：子进程执行 `*(int *)0 = 1`，地址 0 未映射，触发 store page fault；
- TEST-3b：子进程 `sbrk(4096)` 后读取 `p[8192]`，越过已分配页，触发 load page fault；
- 父进程两次 `wait(&st)`，检查 status 为 `-1`。

实测输出：

```text
TEST-3a PASS: wild write killed (status=-1)
TEST-3b PASS: beyond-sbrk access killed (status=-1)
```

为了直接覆盖“从用户栈下方越界写”的 self-test 要求，另加 `user/guardtest.c`：子进程读取当前 `sp`，对 `sp` 所在栈页的下方一页写入，命中 guard page 后触发 store page fault；父进程 `wait()` 得到非零状态。实测输出：

```text
GUARDTEST PASS: stack guard write killed (status=-1)
```

### 9.2 用例 B：跨物理页边界的受控拷贝

设计：`sbrk` 申请两页，在 `p + PGSIZE - 4` 处写入 8 字节模式，使 `write(1, p + PGSIZE - 4, 8)` 的源缓冲区跨越两个用户页；期望 `copyin()` 分两页正确搬运，输出 `ABCDEFGH`，且不越界、不 panic。

实现：`user/copytest.c`

实测输出：

```text
COPYTEST output: ABCDEFGH
COPYTEST PASS: cross-page copyin completed
```

如果 `copyin()` 只校验首地址或错误地按首尾连续物理地址复制，第二页的数据会错位；该用例正好覆盖“页尾剩余字节 + 下一页起点”的边界。

### 9.3 测试汇总

| 程序 | 覆盖点 | 实测结果 |
|---|---|---|
| `badecall` | 未知系统调用号返回 -1，不 panic | `TEST-1 PASS: unknown syscalls all return -1` |
| `badaccess` | guard/越界访存安全终止 | `TEST-3a PASS`、`TEST-3b PASS` |
| `guardtest` | 用户栈下方 guard page 写入被终止 | `GUARDTEST PASS` |
| `memcap` | 配额耗尽返回错误、已有分配可用、内核存活 | `TEST-4 PASS: OOM returned error after 2031 chunks; existing alloc intact; kernel alive` |
| `copytest` | 跨页 copyin | `COPYTEST PASS` |

`bufstorm` 属于 lab2 的字符流输入回归，仍随 `UPROGS` 构建并可交互运行；lab3 主回归不把它作为固定步骤，因为字符流语义下的一次 `read` 可能只取得半行，测试程序按 `read` 次数计行，容易把剩余输入留给 shell，干扰后续用例。

### 9.4 自动化回归

`support/regression.txt` 依次发送 `hi`、`badecall`、`badaccess`、`guardtest`、`copytest`、`memcap`，运行：

```powershell
$env:Path = 'D:\msys64\ucrt64\bin;' + $env:Path
& 'D:\python10\python.exe' support\inject_uart.py --tree . --script support\regression.txt --log regression.log
```

结果为 7 个 `EXPECT` 全部 PASS，退出码 0，日志中没有 panic。

### 9.5 时钟/忙循环检查

`support/spin-check.txt` 发送 `spin`，等待 `spin 0` 和后续非零计数，验证用户忙循环期间定时器中断和 UART 仍然工作。实测两条 `EXPECT` 均 PASS。该检查验证的是中断稳定性；真正的抢占式并发调度留待 lab4。

### 9.6 Banner 校验

```powershell
& 'D:\python10\python.exe' check_expect.py 2024302111297
```

输出 `[ok] 形状校验通过(sid=2024302111297, 协议1)`。`expect_banner.txt` 与内核实际输出的 72 字节逐字节一致，协议 1 要求每个字节（包括末尾换行）后跟 `.`，banner 专用通道不会把点号污染到用户程序正文。

---

## 10. 已知约束与后续实验边界

1. **内核 RWX**：`kernel.ld` 未导出 `etext`，目前把内核镜像和 RAM 统一映射为 R+W+X；用户态仍因缺少 `PTE_U` 无法访问内核。
2. **EAGER sbrk**：未实现 LAZY/缺页按需分配；`sbrk` 请求即分配物理页，失败整体回滚。
3. **同步进程模型**：`wait/exit` 采用父子同步切换，没有真实时间片轮转；`spin` 会占住 CPU，shell 不会并发获得调度。lab4 将引入调度器。
4. **无 argv 传递**：当前 `exec` 只解析程序名，不把 `argv`/`argc` 搬到新用户栈；课程提供的用户程序均不依赖参数。
5. **无 COW/共享内存/文件系统**：这些分别属于 lab5/lab6；本实验的 `fork` 是逐页深拷贝。
6. **CLINT 未映射**：时钟使用 `rdtime`/`stimecmp` CSR，不访问 CLINT MMIO。

---

## 11. 思考题备忘

1. **TLB 未命中要访问几次物理内存？** SV39 三级页表最坏需要 3 次页表页访问，再加 1 次目标数据访问，共 4 次；如果页表项本身被缓存，次数会减少。现代处理器用 TLB、多级 TLB、页表遍历缓存和硬件预取降低开销。
2. **为什么先清 PTE 再释放物理页？** PTE 是硬件翻译入口。先 `kfree()` 会让页进入空闲池，随后可能被分配给其他进程；如果旧 PTE 仍然有效，原进程或陈旧 TLB 仍能读写新所有者的页，造成 use-after-free、跨进程数据泄漏或内核崩溃。正确顺序是清 PTE、`sfence.vma` 使旧翻译失效，再释放物理页。
3. **copyin 与 memcpy 的本质区别？** `memcpy` 只假设源/目的都是当前特权级可直接访问的连续内存；`copyin` 必须逐页验证用户地址、`PTE_U`、读/写权限和 `sz` 边界。若内核直接对用户指针调用 `memcpy`，恶意程序可以传入内核地址、guard 洞或伪造的跨页指针，诱导内核读写任意物理内存，破坏进程隔离甚至执行提权攻击。

---

## 12. 关键文件与验收清单

| 文件 | 内容 |
|---|---|
| `kernel/kalloc.c` | 位图分配器、降序分配、配额、清零/垃圾填充、空闲计数 |
| `kernel/vm.c` | `walk`、`mappages` 回滚、`uvmunmap/uvmfree/uvmcopy`、`copyin/copyout/copyinstr`、`dump_pagetable` |
| `kernel/proc.c` | 用户地址空间装载、fork/exec/sbrk/wait/exit、页表与陷入帧生命周期 |
| `kernel/trap.c` | `usertrap` 首行切 `kernelvec`、用户异常终止、返回跳板 |
| `kernel/kernelvec.S` | 内核态中断向量 |
| `kernel/trampoline.S` | `uservec`/`userret` 同址跳板 |
| `Makefile` | VM 源文件、派生用户链接脚本、程序元数据表、官方测试程序 |
| `user/copytest.c` | 跨页 copyin 自测 |
| `user/guardtest.c` | 用户栈下方 guard page 自测 |
| `support/regression.txt` | 自动化回归脚本 |
| `support/spin-check.txt` | 忙循环/定时器检查 |
| `expect_banner.txt` | 协议 1 Banner 期望 |

验收时应能完成：

- 启动后输出 `DUMP-PAGETABLE` 块，并解释代码页 `r-xu`、数据页 `rw-u`、guard 空洞、陷入帧 `rw--`、跳板页 `r-x-`；
- 运行 `badaccess`，看到 TEST-3a/3b PASS；
- 运行 `memcap`，看到 TEST-4 PASS 且 shell 仍可用；
- 运行 `copytest`，看到 `ABCDEFGH` 与 PASS；
- 运行 `support/inject_uart.py`，全部 EXPECT PASS；
- 用 `check_expect.py` 确认 Banner 协议。
