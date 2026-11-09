# Lab2 设计笔记 —— 陷入与中断:syscall 与控制台

学号:2024302111297　个性化参数:`LAB2_BUF_SEMANTICS=1`(字符流)、
`LAB2_BUF_SIZE=64`、`LAB2_TICK=2`、`LAB1_BANNER_PROTOCOL=1`、`LAB1_STACK_KB=8`。

本轮完成的四个阶段:陷入/返回框架 → 系统调用分发 → 中断驱动控制台输入 →
装载并运行 sh。下面按"能照着写代码的精度"把两张图和设计取舍写清楚,
并把 lab4/lab5/lab3 的欠债逐条记账。

---

## 1. 两张图

### 图 1 一次 `write(1, "hi", 2)` 的完整往返

```text
[U 态] 用户程序 user/printf.c: putc()
       a0=1(fd)  a1=&"hi"(用户栈/镜像里的地址)  a2=2(len)  a7=SYS_write(16)
       ecall
────────────────────────────────────────────────────────────────────────
硬件只做三件事(其余全是软件的事):
       sstatus.SPP ← U(记住"从哪来")、sstatus.SIE → SPIE、SIE ← 0
       sepc ← ecall 这条指令的地址(注意:是 ecall 本身!)
       scause ← 8(用户态 ecall)、PC ← stvec = uservec
       通用寄存器一个没动,栈也没换(还是用户栈)
────────────────────────────────────────────────────────────────────────
[仍是 U 态页表、sp 还是用户栈] uservec(赠送件 trampoline.S)
       csrw sscratch, a0          # a0 先藏进 sscratch,腾出 a0 当基址
       li   a0, TRAPFRAME         # 0x87FFE000(编译期常量)
       sd ra,40(a0) / sd sp,48 / sd gp,56 / ...   # 31 个寄存器按固定偏移落盘
       csrr t0, sscratch; sd t0,112(a0)           # 补上刚才藏起来的 a0
       ld sp, 8(a0)               # 换成内核栈顶 ← 至此栈才切换
       ld t0,16(a0)               # t0 = usertrap
       ld t1, 0(a0); csrw satp,t1 # 换内核页表(lab2 恒为 0,空操作)
       jalr t0                    # 进 C 代码
────────────────────────────────────────────────────────────────────────
[S 态、内核栈] usertrap()
       UTRAPFRAME->epc = r_sepc()        # 记下用户 PC
       scause == 8 → UTRAPFRAME->epc += 4   ★跳过 4 字节的 ecall
       syscall()
         num = 现场页->a7 = 16 → syscalls[16] = sys_write
         argint(0,&fd) / argaddr(1,&buf) / argint(2,&n)  ← 就是现场页的 a0/a1/a2
         sys_write: 逐字节 consputc() → UART THR
         返回 n,写回 UTRAPFRAME->a0(仅当没发生进程切换)
       usertrapret()
         现场页头部 4 项:kernel_satp / kernel_sp / kernel_trap / kernel_hartid
         sstatus.SPP ← 0(sret 回 U 态)、SPIE ← 1(U 态中断使能)
         sepc ← 保存的 epc(下一条指令)
         userret(0)                 # a0 = 用户页表
────────────────────────────────────────────────────────────────────────
userret: csrw satp,a0(=0); li a0,TRAPFRAME; 恢复 31 个寄存器(含 a0=2);
         sret → 特权级回 U,sstatus.SIE ← SPIE = 1,PC ← sepc
────────────────────────────────────────────────────────────────────────
[U 态] 从 ecall 的下一条指令继续跑,a0 = 2(write 的返回值)
```

★ 这一行漏了会怎样:sepc 仍指向 ecall,`sret` 回去又执行一次 ecall,
于是终端里同一串字符无限重复输出 —— 说明书第 3 节表格第一行的症状。

### 图 2 一次键盘中断的完整路径

```text
键盘 → QEMU 串口模型 → UART0 收到字节:置 LSR.DR=1,拉高 IRQ 10
      (中断使能链路四环,逐环都要通:
       UART.IER.RDI=1 → PLIC 优先级=1 且 SENABLE bit10 且 SPRIORITY=0
       → sie.SEIE=1 → sstatus.SIE=1(只有在 U 态时才是 1))
PLIC → S 态外部中断:scause = 0x8000_0000_0000_0009
      (此时用户程序正在跑,stvec = uservec)
uservec: 31 个寄存器落进现场页,sp ← 内核栈顶,jalr usertrap
      ↓
usertrap: scause>>63 != 0 → devintr()
      ↓
devintr: (scause & 0xff) == 9 → plic_claim() == UART0_IRQ(10) → uartintr()
      ↓
uartintr: while ((c = uartgetc()) >= 0) console_push(c)
       ├─ console_push: '\r'→'\n';退格则 wpos-- 并回显 "\b \b"
       ├─ buf_push(c):缓冲区满则丢弃;否则 buf[w%SIZE]=c, w++
       ├─ if (buf_ready()) cons_wakeup = 1     ← "唤醒"读者(字符流:非空)
       └─ consputc(c)                        ← 回显(明文通道)
      ↓
plic_complete(10)  ← 不写回 SCLAIM,PLIC 就不会再上报同一个 IRQ
      ↓
usertrapret → userret → sret 回到被打断的用户程序继续跑
      ↓
(若有进程正卡在 consoleread 的等待里,它下一轮自旋就会发现 cons_wakeup=1)
```

---

## 2. 硬件分工与 trampoline 协议(阶段一)

- `ecall` 只做:切到 S 态、`sepc ← ecall 地址`、`PC ← stvec`。寄存器不存、
  栈不换 —— 这正是 `trampoline.S` 存在的理由,也是它必须是汇编的理由:
  它要在**还没有可用栈**的情况下只用寄存器(sscratch 借 a0)把现场存到
  一个固定地址,然后才换栈、换页表、进 C。C 函数做不到这件事(函数入口
  本身就假设栈可用)。
- `sret` 只做:特权级降回 `sstatus.SPP`、`PC ← sepc`、`SIE ← SPIE`。
- C 侧只负责"协议的另一半":`struct trapframe` 的字段顺序/大小与
  `trampoline.S` 的偏移一一对应(0/8/16/24/32 + 40..280),共 288 字节;
  `usertrap()` 处理完把现场页头部四项填好,`usertrapret()` 设置
  `sstatus.SPP/SPIE` 与 `sepc`,再由 `userret` 收尾。
- **阶段一的自测**:`userinit()` 做的就是说明书建议的那件事 ——
  在 `main()` 里人为构造一份 trapframe(epc 指向 sh 的第一条指令),
  调 `usertrapret()` 走一次 `sret` 往返。排障时把 epc 改成一段只做
  `ecall` 的代码,就能单独验证"陷入—返回"这条通道。

---

## 3. 设计决策(验收抽题区)

### 3.1 trapframe 放哪、多大、每进程一份还是全局?谁在何时切换它?

- **多大**:288 字节。头部 5 个 uint64(其中 24 是 epc、32 是 hartid)+
  31 个通用寄存器槽(x0 不用存)。
- **放哪**:`TRAPFRAME = PHYSTOP - 2*PGSIZE = 0x87FFE000`,RAM 顶部倒数
  第二页。lab2 是 `satp=0` 直映,而 `trampoline.S` 用 `li a0, TRAPFRAME`
  **编译期常量**直接寻址,所以它必须是一块真实存在的物理内存;xv6 原版的
  `TRAMPOLINE - PGSIZE = 0x3FFFFFE000` 在 lab2 没有任何内存映射到那里,
  一存就访问异常。这是 lab2 相对 xv6 的**唯一结构性改动**,已写进
  `kernel/memlayout.h` 并注明 lab3 恢复顶端布局。
- **每进程一份还是全局**:**现场页全局一份**;每个进程在 `struct proc.tf`
  里持有自己的副本。原因是 trampoline 里的常量无法按进程改写 —— 同一
  物理地址不可能"对每个进程各指向一份"。xv6 靠页表把同一个虚拟地址
  `TRAPFRAME` 映射到不同物理页解决这件事,lab3 有了 sv39 就能回到那个形态。
- **谁在何时切换**:`proc_switch(from, to)` —— 调用者必须处于
  "自己的现场已经被 uservec 存进现场页"的状态(即从 `usertrap()` 进来的
  系统调用内部);它把现场页 `memmove` 回 `from->tf`,再把 `to->tf`
  `memmove` 进现场页。发生点只有两处:`sys_wait()` 把 CPU 交给子进程、
  `sys_exit()` 把 CPU 还给正在等它的父进程。
- 推论:`usertrapret()` 每次都要重写现场页头部四项(kernel_satp/kernel_sp/
  kernel_trap/kernel_hartid),因为它们描述的是"当前进程"下次陷入要用的
  运行环境,而"当前进程"是会变的。

### 3.2 输入缓冲的数据结构、锁、检查-入睡、行/字符流差别

- **结构**:`static uchar buf[LAB2_BUF_SIZE]` + 两个**单调递增**的下标
  `rpos/wpos`,索引取模 `buf[i % SIZE]`。`wpos - rpos` 的无符号差值就是
  当前元素个数(计数回绕也成立),`== 0` 判空、`== SIZE` 判满。
- **满**:丢弃新字符(不阻塞、不报错),`bufstorm` 灌超长行时属于文档化行为。
- **锁**:lab2 单 hart,且**S 态始终关中断**(`sstatus.SIE=0`)。生产者
  `uartintr()` 与消费者 `consoleread()` 都只可能在"中断已关"的 S 态执行,
  所以环形缓冲区不需要自旋锁 —— **关中断就是这把锁**。代价:一旦 lab4
  允许 S 态开中断或上多核,`rpos/wpos` 必须补锁,否则会出现"下标撕裂"。
- **检查-入睡怎么落地**(`consoleread`):

  ```c
  while (got < n) {
    if (!buf_ready()) {                    /* 检查 */
      if (got > 0) break;                  /* 已交付的部分先返回 */
      cons_wakeup = 0;                     /* 入睡(lab2 版) */
      while (!cons_wakeup) {               /* 轮询等待唤醒 */
        int c = uartgetc();                /* 关中断轮询 UART */
        if (c >= 0) console_push(c);       /* 走"中断同款"通路入缓冲+回显 */
      }
      continue;
    }
    p[got++] = buf_pop();
    ...
  }
  ```
  中断侧对应的一行:`buf_push()` 里 `if (buf_ready()) cons_wakeup = 1;`。
  所以 `cons_wakeup` 就是"睡眠者"的占位符,lab4 把它换成
  `sleep(chan)/wakeup(chan)`(chan 取缓冲区地址)即可,条件判断不用动。
- **行缓冲 vs 字符流的差别只在 `buf_ready()` 一个函数里**:

  ```c
  #if LAB2_BUF_SEMANTICS == 0   /* 行缓冲 */
    return 缓冲区里存在 '\n';
  #else                         /* 字符流(= 本学号的参数) */
    return !buf_empty();
  #endif
  ```
  中断侧(何时置唤醒)和读取侧(何时返回)都调这同一个函数,语义不可能不一致。
- **"只改一处"会出什么问题**:
  - 只把中断侧改成行缓冲(唤醒条件含 `'\n'`)、读取侧仍"非空即返回" →
    读者不等行尾就返回半行,行缓冲语义形同虚设(应用层重新拼行)。
  - 只把读取侧改成行缓冲(等 `'\n'`)、中断侧仍"非空就唤醒" →
    读者在缓冲区里还没有 `'\n'` 时被唤醒;若是真的 sleep/wakeup,
    醒来发现条件不满足又睡回去,而这一次唤醒已经**被吃掉**,
    于是丢唤醒、卡死到下一行到达为止。所以判据必须收在一处。

### 3.3 syscall 分发表与错误码

- **表驱动**(不是 switch):`static uint64 (*syscalls[])(void) = { [SYS_fork] = sys_fork, ... }`,
  稀疏初始化,没列出的调用号是 0。加一个调用只动一行,而且天然不会
  "忘记写 default"。
- **分发**:`num > 0 && num < NELEM(syscalls) && syscalls[num] != 0` 才调用;
  否则 `a0 = (uint64)-1`。**任何未知/未实现调用号都返回确定的 -1,绝不 panic**
  (`badecall` 用 90..99 号验证)。
- **返回值**:统一写回 `UTRAPFRAME->a0`(RISC-V 约定返回值在 a0)。
  唯一例外见 3.3.1。
- **错误码约定**:`-1` = 失败(含"未实现");`>= 0` = 成功,语义随调用而定
  (`write` 返回写出的字节数、`read` 返回读到的字节数、`wait` 返回子进程 pid)。
- 调用号以 `gifts/kernel/syscall.h` 为准(1..22 连续),与 `gifts/user/usys.pl`
  的桩表一一对应,不自创编号。

#### 3.3.1 为什么要判断"本次调用有没有换进程"

```c
struct proc *p = myproc();
... ret = syscalls[num](); 
if (myproc() == p)          /* 没换进程才回填 */
  UTRAPFRAME->a0 = ret;
```

`sys_wait()` 会把现场页换成子进程的,`sys_exit()` 会换成父进程的。
若无条件写 `UTRAPFRAME->a0 = ret`,就会踩掉对方进程的 a0:
- `wait` 的情形会抹掉子进程 fork 时应看到的 `a0 = 0`(子进程以为自己是父进程);
- `exit` 的情形会抹掉父进程 `wait()` 的返回值(父进程拿不到子进程 pid)。

### 3.4 fork 在无页表时代怎么复制用户镜像?复制到哪、谁记账?

- 没有页表就没有"共享的地址空间",**只能整块物理拷贝**:
  第 i 个进程独占 `[USERIMG_BASE + i*USERIMG_SLOT, +64KB)`,平铺二进制放槽底、
  用户栈从槽顶向下生长。`fork` 用 `memmove` 把父进程整个 64KB 槽复制进
  子进程槽(程序 + 静态数据 + 用户栈),再复制一份现场页。
- **谁记账**:进程表 `struct proc procs[NPROC]`(state/pid/parent/xstatus/
  tf/img/imgsize)。**槽位下标 == 进程表下标**,`proc_alloc()` 找到 UNUSED 槽
  并算出 `p->img`,不需要额外分配器。
- **槽基址不同带来的坑(必须处理)**:两个进程的镜像不在同一物理地址,
  所以现场里所有"指向父进程槽"的寄存器值(epc、sp、s0/fp,以及任何指向
  镜像/用户栈的数据指针)都要整体平移。`tf_relocate()` 逐个寄存器检查
  落在 `[from, from+SLOT)` 的值并加上 `to-from`。局限:恰好落在 64KB 窗口
  内的**普通整数**会被误搬 —— 记为 lab2 的债,lab3 给每个进程同一套
  虚拟地址后这条函数直接删除。
- 用户程序按 `user.ld` 从地址 0 链接,但加载到槽底后能原址运行:编译用
  `-mcmodel=medany`,代码和数据的寻址都是 PC 相对(`auipc`)的,只要整个
  二进制落在 ±2GiB 内即可。
- **入口偏移**:平铺二进制的入口未必在偏移 0(官方用例 `badecall.c` 的
  `main` 前面还有静态函数 `ecall_raw`)。构建期用 `nm` 从 ELF 里取 `main`
  的地址,生成与 `_uprog_table` **同序**的 `_uprog_entry[]`,内核按名查到
  下标 i 后取 `_uprog_entry[i]`。见 §7,规则是附加的,没有改任何一个赠送件。

### 3.5 用户指针直接解引用(记账)

`argaddr/argstr/sys_write/sys_read/consoleread` 现在把用户传来的地址当
内核指针直接用。lab2 这样是**合法**的:`satp=0` 下用户内存物理可达,
PMP 又放行了整片物理空间。这是明确的债:lab3 打开 sv39 之后必须换成
`walkaddr + copyin/copyout/copyinstr`,并做区间合法性检查,否则用户程序
传一个内核地址就能直接读写内核。

---

## 4. 思考题

**① ecall/sret 时硬件各做了什么?为什么不在硬件层面自动保存通用寄存器、切换栈?**

`ecall`:`sstatus.SPP ← 当前特权级`、`sstatus.SIE → SPIE` 且 `SIE ← 0`、
`sepc ← ecall 的地址`、`scause ← 原因码`、`PC ← stvec`。`sret`:
特权级 ← `sstatus.SPP`、`PC ← sepc`、`SIE ← SPIE`。
不做保存/切栈的理由:(a) 通用寄存器有 31 个,交给硬件保存就必须规定
"存到哪"(核内的隐藏位置?)—— 那会变成一份昂贵的硬件状态;软件只要
有一个固定地址和一条 `sd` 就能按需保存,还顺便决定了保存布局;
(b) 栈指针换到哪取决于内核/用户地址空间的划分策略(每个进程一份内核栈、
页表形态),这是操作系统的策略,硬件不该越权决定。硬件只保留最小必要
状态(SPP/SPIE/SEPC/SCAUSE),其余留给软件,这正是 RISC-V 的分工。

**② 处理系统调用时对 sepc 做什么?为什么?时钟中断要同样处理吗?**

系统调用:`sepc += 4`。因为 ecall 异常时 `sepc` 记录的是 **ecall 指令本身**
的地址;不推进就 `sret` 回同一条 `ecall`,用户程序原地无限重复系统调用
(典型症状:write 后同一串字符反复输出)。ecall 固定是 4 字节,所以加 4。
时钟中断**不需要**,而且**不能**:中断打断的是"尚未执行完"的那条指令,
`sepc` 指向的就是它,原样返回才能继续执行;若也加 4,就会跳过一条用户
指令 —— 用户程序的行为会随机错乱。`usertrap()` 里只有 `scause == 8`
这一支才动 `epc`。

**③ 行缓冲/字符流两种语义在中断处理和读取函数里各造成什么差别?只改一处会怎样?**

差别只有一处:`buf_ready()` 的判据(字符流 = 非空;行缓冲 = 缓冲区里
含 `'\n'`)。中断侧用它决定"何时置唤醒",读取侧用它决定"何时返回"。
只改中断侧 → 读者按旧判据提前返回(或行缓冲下彻底等不到,因为唤醒了
又立刻发现不含 `'\n'`);只改读取侧 → 唤醒条件与等待条件不一致,在真正
的 sleep/wakeup 实现里会**丢唤醒**并卡死。所以本轮把判据收敛成一个
函数,两侧共用。详见 3.2。

---

## 5. 自测用例与回归

| 用例 | 内容 | 判据 |
|---|---|---|
| `badecall`(官方) | 90..99 号非法调用 | 全部返回 -1,打出 `TEST-1 PASS`,sh 存活 |
| `bufstorm`(官方) | 输入缓冲边界/语义 | `BUFSTORM lines=4`;超长行丢字属文档化行为,内核必须完好 |
| `bufcap`(自写) | A: `read(0,buf,LAB2_BUF_SIZE)` 恰好等于缓冲区容量;B: 缓冲区空时 read 阻塞到有字符;C: `read(..,1)` 短读 | A 的返回值落在 `1..64`;B 必须"等到输入才返回";C 返回 1 |
| 回归 | 启动 banner | 逐字节等于 `expect_banner.txt`(协议 1) |
| 回归 | `hi` / `spin` | `hi` 打出 pid;`spin` 期间敲键盘,回显与 spin 输出交错不乱 |
| 链路 | 非法调用号不 panic | 见 `badecall` |

驱动方式(有 RISC-V 工具链的机器上):

```sh
make qemu                      # 看到 banner → sh> 提示符
# 在 sh> 里:hi / spin / badecall / bufstorm / bufcap
python3 support/inject_uart.py --tree . --script storm.txt --log out.txt
# storm.txt 例:
#   EXPECT hi: user program running
#   SEND hi
#   EXPECT sh>
#   SEND badecall
#   EXPECT TEST-1 PASS
#   SEND bufstorm
#   REPEAT 4 0123456789...
#   EXPECT BUFSTORM
```

`bufstorm` 的默认用例是把 4 行(其中至少一行长于 `LAB2_BUF_SIZE=64`)
灌进去:缓冲区满后新字符被丢弃,因此 `bytes` 可能小于注入总量,但
`lines` 必须等于 4,内核不得 panic。

---

## 6. 已知局限与欠债台账

| # | 局限 | 还账轮次 |
|---|---|---|
| 1 | "假调度":任一时刻只有一条可运行上下文,`wait()` 直接把 CPU 交给子进程,`exit()` 直接还给父进程;没有时间片轮转,`yield()` 是空函数,`LAB2_TICK` 只用于 tick 记账 | lab4 |
| 2 | 阻塞式读用"关中断 + 轮询 UART"代替 `sleep/wakeup`(S 态全程关中断,stvec 只有 uservec 一个入口,不能在 S 态被中断) | lab4(补 kernelvec) |
| 3 | 环形缓冲区无锁(靠 S 态关中断保护) | lab4(开中断/多核后必须加锁) |
| 4 | 用户指针直接解引用,无 copyin/copyout、无地址合法性检查 | lab3 |
| 5 | `exec(name, argv)` 忽略 argv | lab5 |
| 6 | 进程槽固定 64KB、无 kalloc/无动态内存、`sbrk` 未实现(返回 -1) | lab3 |
| 7 | `tf_relocate()` 对"恰好落在槽窗口内的整数"会误搬 | lab3(同地址虚拟空间后删除) |
| 8 | 没有 `kill`/信号,`spin` 跑起来只能 `Ctrl-A x` 退 QEMU | lab4+ |
| 9 | `pause(n)` 未实现(返回 -1):S 态关中断时没法等 tick 前进 | lab4 |
| 10 | `TRAPFRAME`/`USERIMG_BASE` 是硬编码物理常量(依赖 QEMU virt 默认 128MB RAM) | lab3(sv39 恢复顶端布局) |

---

## 7. 构建系统与接口改动清单

1. **Makefile**:把 `gifts/Makefile.upgrade` 的规则整段并入(原文照抄,
   三固定项 `make kernel` / `make qemu` / `course_sid.h` 依赖链不动),
   并把 `kernel/userimg.o` 等新目标加进 `OBJS`。
   **唯一改动:UFLAGS 追加 -mno-relax**(理由见 §9;不加则平铺二进制无法搬迁)。
2. **新增 `kernel/userentry.S` 生成规则(附加件,不改赠送规则)**:
   用 `nm` 从 `user-flat/<p>.elf` 取 `main` 的地址,生成与 `_uprog_table`
   同序的 `_uprog_entry[]`。没有它,`badecall` 会从 `ecall_raw` 开始执行。
3. **`kernel/kernel.ld`**:显式收编跳板段并页对齐。**段名陷阱**:`trampoline.S`
   写的是 `.section trampsec`(段名**无前导点**),所以必须写 `*(trampsec)`
   (参考 xv6 亦然);若写成 `*(.trampsec)` 则匹配不到,跳板代码会退化成
   链接器自由摆放的 orphan 段(宿主 GAS 实测生成的段名就是 `trampsec`)。
   另加 `_trampoline` 记号,lab3 把它映射到 TRAMPOLINE 时直接取用。
4. **`kernel/memlayout.h`**:lab2 的 `TRAPFRAME` 与 `USERIMG_BASE/USERIMG_SLOT`。
5. **`kernel/start.c`**:M 态放行 sstc(`menvcfg.STCE`)与 `mcounteren.TM`。
6. **`kernel/console.c`**:分成两条输出通道 —— `consputc()`(明文,供
   内核诊断/用户 write/回显)与 banner 专用协议通道(`LAB1_BANNER_PROTOCOL`)。
   这样 banner 逐字节不变(回归通过),而用户程序输出不会被 `.` 污染
   (否则 `bufstorm` 的 EXPECT 校验必然失配)。
7. 新增内核文件:`trap.c`(陷入/返回/中断分发/时钟)、`syscall.c`(参数解包
   + 分发表)、`sysproc.c`(getpid/uptime/write/read)、`proc.c`(进程表/fork/
   wait/exit/exec/装载)、`plic.c`、`string.c`、`defs.h`、`proc.h`。
8. 新增用户自测程序 `user/bufcap.c`,并把官方测试 `badecall.c`/`bufstorm.c`
   放进 `user/` 一起内嵌(`UPROGS = sh hi spin badecall bufstorm bufcap`)。
---

## 8. 验收脚本的两个环境前提(与内核无关)

`support/inject_uart.py` 直接跑会踩到两件事,先说清楚,免得误判成内核故障:

1. 它的 `QEMU_ARGS` 里带了 `-drive file=fs.img,if=none,...`。lab2 没有磁盘,
   树根若不存在 `fs.img`,QEMU 会直接启动失败。先在树根造一个 0 字节文件即可:
   `python -c "open('fs.img','wb').close()"`(`make qemu` 不受影响,它不带 `-drive`)。
2. 它等待的提示符是 `"$ "`,而赠送的 `sh.c` 打印的是 `sh> `,所以脚本开头必然
   先报一次 `boot TIMEOUT`(等满 10s 后继续执行)。后续 `EXPECT` 仍然有效,
   建议第一条就写 `EXPECT sh>`,作为"真的进了 shell"的判据。
---

## 9. 实机联调记录(装上 riscv64-unknown-elf-gcc 16.1.0 + QEMU 11.1.1 之后)

第一次真正 `make` / `make qemu` 抓到四个缺陷,全部已修:

| # | 现象 | 根因 | 修法 |
|---|---|---|---|
| 1 | 编译失败:`kernel/trap.c:157: error: 'noreturn' function does return [-Werror]` | `usertrapret()` 声明了 `noreturn`,但它调用的 `userret` 没声明,GCC 16 认为它可能返回 | `trap.c` 中把 `userret` 声明为 `__attribute__((noreturn))` |
| 2 | `sh` 一执行命令就 `usertrap(): unexpected scause 0x7 pid=1`,`stval=0x740`(store access fault) | **链接器松弛**:用户程序从 0 链接却被搬到 `0x81000000` 执行,GCC 生成的 `auipc+addi`(PC 相对)被 RISC-V 链接器折叠成 `addi rd,zero,0x740`(绝对寻址),bss 引用因此指向物理 `0x740` | `Makefile` 的 `UFLAGS` 追加 **`-mno-relax`**;此后 `buf` 恢复为 `auipc` 相对,平铺二进制可任意搬迁 |
| 3 | `exec hi failed`(内嵌表查不到名字) | 赠送的 `sh.c` 用 `gets()` 取整行,交过来的名字带行尾 `'\n'`;表里存的是裸名字 | `sys_exec` 先取"第一个词"(等价于解析 `argv[0]`)再查表 |
| 4 | 串口打出 `proc 0 exit: parent -1 is not waiting` 后整机停死 | `sys_exit` 里 `proc_switch()` 只是**换现场页**并正常返回,代码穿透到函数尾部的 `printf/halt`;此刻 `p` 已被回收(pid=0/parent=-1),于是用子进程身份 `halt` | `proc_switch(p, par)` 之后立刻 `return 0`,让 `usertrapret()` 带着父进程现场回 U 态 |

**实机验收证据**(全部通过;实机 banner 与 `expect_banner.txt` 逐字节一致,72 字节):

```
O.S.L.A.B.1. ... .m.o.d.9.7.=.0.x.5.7.
.sh> hi
hi: user program running, pid=2
sh> badecall
TEST-1 PASS: unknown syscalls all return -1
sh> hi
hi: user program running, pid=4      <- pid 3 被 badecall 用掉:pid 记账与槽位回收都正常
sh> bufcap
bufcap: A got=1 ... bufcap: done     <- 字符流语义:有 1 字节就返回(短读)
sh> bufstorm
BUFSTORM lines=4 bytes=130           <- 超长行溢出丢字(文档化行为),内核完好
sh> spin
spin 0 ... KEYSspin 631 ...          <- 忙循环期间键盘回显与程序输出交错不乱
```

**`-mno-relax` 不是可选项**:lab3 起跳板与内核会映射到与链接地址不同的虚拟地址,缺了它同样会退化成绝对寻址而崩。