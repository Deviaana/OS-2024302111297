#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "proc.h"
#include "defs.h"

/* trampoline.S(赠送件)提供:
 *   uservec —— U 态陷入的唯一入口(把 31 个寄存器存进现场页后跳到 usertrap)
 *   userret —— 从现场页恢复寄存器并 sret 回 U 态
 * 两者是同一份协议的两半,偏移量必须与 proc.h 的 struct trapframe 对齐。*/
extern char uservec[];
extern void userret(uint64 satp) __attribute__((noreturn));

/* 时钟中断周期。QEMU virt 的 time 基准约 10MHz,1000000 ≈ 0.1s */
#define TIMER_INTERVAL 1000000

static uint64 ticks;

void
trapinithart(void)
{
  w_stvec((uint64)uservec);               /* 陷入入口:跳板 uservec */
  w_sie(r_sie() | SIE_SEIE | SIE_STIE);   /* 放行外部(PLIC/UART)与时钟中断 */
  w_stimecmp(r_time() + TIMER_INTERVAL);  /* 装上第一次时钟中断(sstc) */
}

uint64
uptime(void)
{
  return ticks;
}

/* lab2 的"假调度":任一时刻只有一条可运行的用户上下文,
 * 所以"让出 CPU"等价于什么都不做(见《设计笔记-lab2.md》§3)。
 * lab4 会把它换成真正的轮转与 sleep/wakeup。 */
static void
yield(void)
{
}

/* 时钟中断:累计 tick、重新装弹(写 stimecmp 同时清掉中断请求),
 * 每 LAB2_TICK 个 tick 触发一次"时间片到"的假让出。*/
static void
timerintr(void)
{
  ticks++;
  w_stimecmp(r_time() + TIMER_INTERVAL);

  if (ticks % LAB2_TICK == 0)
    yield();
}

void
halt(void)
{
  for (;;)
    asm volatile("wfi");
}

void
panic(const char *msg)
{
  intr_off();
  printf("panic: %s\n", msg);
  halt();
}

/* 中断分类:9 = S 态外部中断(PLIC),5 = S 态时钟中断。
 * 返回值沿用 xv6 约定:2 = 时钟,1 = 其它设备,0 = 不认识。 */
static int
devintr(void)
{
  uint64 scause = r_scause();

  if ((scause & 0xff) == 9) {
    int irq = plic_claim();

    if (irq == UART0_IRQ)
      uartintr();
    else if (irq)
      printf("devintr(): unexpected irq=%d\n", irq);

    if (irq)
      plic_complete(irq);
    return 1;
  } else if ((scause & 0xff) == 5) {
    timerintr();
    return 2;
  }
  return 0;
}

/*
 * 陷入分发。调用链:
 *   U 态 ecall/中断 → uservec → usertrap() → (usertrapret → userret → sret)
 */
void
usertrap(void)
{
  struct proc *p = myproc();

  if ((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  /* uservec 已把 31 个通用寄存器存进现场页,这里补记用户 PC */
  UTRAPFRAME->epc = r_sepc();

  uint64 scause = r_scause();

  if (scause == 8) {
    /* ---- 系统调用 ---- */
    UTRAPFRAME->epc += 4;   /* ecall 长 4 字节:不推进就会原地重复调用 */
    syscall();              /* 解包参数、查表分发、把返回值写回 a0 */
  } else if ((scause >> 63) != 0) {
    /* ---- 中断(时钟/UART) ---- */
    if (devintr() == 0)
      panic("usertrap: unexpected interrupt");
  } else {
    /* ---- 其它异常:打印排障三件套后停机 ---- */
    printf("usertrap(): unexpected scause 0x%p pid=%d\n", scause, p->pid);
    printf("            sepc=0x%p stval=0x%p\n", r_sepc(), r_stval());
    halt();
  }

  usertrapret();
}

/*
 * 返回 U 态前的环境准备:
 *   1) 把"下次陷入要用"的内核运行环境写进现场页头部;
 *   2) 设 sstatus.SPP=U、SPIE=1(让 sret 之后 U 态中断是开的);
 *   3) sepc = 用户下一条指令;
 *   4) 交给 userret 恢复寄存器并 sret。
 */
void
usertrapret(void)
{
  UTRAPFRAME->kernel_satp = r_satp();      /* lab2 恒为 0(bare 模式) */
  UTRAPFRAME->kernel_sp = KSTACK_TOP;      /* 下次陷入用的内核栈 */
  UTRAPFRAME->kernel_trap = (uint64)usertrap;
  UTRAPFRAME->kernel_hartid = 0;

  uint64 x = r_sstatus();
  x &= ~SSTATUS_SPP;                       /* sret 之后回 U 态 */
  x |= SSTATUS_SPIE;                       /* 恢复 U 态中断使能 */
  w_sstatus(x);

  w_sepc(UTRAPFRAME->epc);

  /* S 态保持关中断:stvec 只有 uservec 一个入口,它假定"从 U 态来",
   * 在 S 态被打断会让它把内核寄存器当成用户现场存起来。
   * 阻塞式等待(csoleread)因此用轮询而不是 sleep,见 §3 设计笔记。*/
  intr_off();

  userret(0);                              /* a0 = 用户页表;内部 sret,不返回 */
}