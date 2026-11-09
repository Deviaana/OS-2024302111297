#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "proc.h"
#include "defs.h"

extern char trampoline[];
extern char uservec[];
extern char userret[];
extern char kernelvec[];

#define TIMER_INTERVAL 1000000

uint64 ticks;
struct spinlock tickslock;

void
trapinit(void)
{
  initlock(&tickslock, "ticks");
}

void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
  w_sie(r_sie() | SIE_SEIE | SIE_STIE);
  w_stimecmp(r_time() + TIMER_INTERVAL);
}

uint64
uptime(void)
{
  return ticks;
}

static void
timerintr(void)
{
  w_stimecmp(r_time() + TIMER_INTERVAL);
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
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

void
usertrap(void)
{
  struct proc *p = myproc();
  uint64 scause;

  /* uservec switches page tables but deliberately does not change stvec. */
  w_stvec((uint64)kernelvec);

  if ((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  p->trapframe->epc = r_sepc();
  scause = r_scause();

  /* scause == 8 表示用户态执行 ecall。a7 中是系统调用号；
   * epc 先指向下一条指令，再由 syscall() 查分发表跳转到具体实现。 */
  if (scause == 8) {
    p->trapframe->epc += 4;
    syscall();
    p = myproc();
  } else if ((scause >> 63) != 0) {
    int which = devintr();

    if (which == 0)
      panic("usertrap: unexpected interrupt");
    if (which == 2) {
      int expire;

      p = myproc();
      acquire(&p->lock);
      p->cpu_ticks++;
      p->last_slice_ticks++;
      expire = p->last_slice_ticks >= LAB2_TICK;
      release(&p->lock);
      if (expire) {
        yield();
        p = myproc();
      }
    }
  } else {
    /* Invalid access, guard-page hit, or another user exception: terminate
     * only this process and let the parent's wait() recover it safely. */
    proc_exit(-1);
    p = myproc();
  }

  if (p->killed)
    proc_exit(-1);

  usertrapret();
}

void
usertrapret(void)
{
  struct proc *p = myproc();
  struct trapframe *tf = p->trapframe;

  tf->kernel_satp = r_satp();
  tf->kernel_sp = p->kstack + PGSIZE;
  tf->kernel_trap = (uint64)usertrap;
  tf->kernel_hartid = r_tp();

  uint64 x = r_sstatus();
  x &= ~SSTATUS_SPP;
  x |= SSTATUS_SPIE;
  w_sstatus(x);
  w_sepc(tf->epc);

  intr_off();
  w_stvec(TRAMPOLINE + ((uint64)uservec - (uint64)trampoline));

  uint64 satp = MAKE_SATP(p->pagetable);
  void (*jump)(uint64) =
    (void (*)(uint64))(TRAMPOLINE + ((uint64)userret - (uint64)trampoline));
  jump(satp);
  __builtin_unreachable();
}

void
kerneltrap(void)
{
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();

  if ((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");

  if (devintr() == 0) {
    printf("kerneltrap: scause=0x%p sepc=0x%p stval=0x%p\n",
           r_scause(), r_sepc(), r_stval());
    panic("kerneltrap");
  }

  w_sepc(sepc);
  w_sstatus(sstatus);
}
