#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "proc.h"
#include "syscall.h"
#include "defs.h"

uint64
sys_getpid(void)
{
  return (uint64)myproc()->pid;
}

uint64
sys_uptime(void)
{
  return uptime();
}

/*
 * pause(n)（SYS_pause = 13）
 * 参数：n 为需要等待的时钟 tick 数。
 * 跳转：用户 pause() -> usys.S 桩 -> ecall -> syscall() 分发表 -> 本函数。
 * 实现：在 tickslock 下检查全局 ticks；未到期就 sleep(&ticks, &tickslock)。
 * 时钟中断 timerintr() 每次 ticks++ 后 wakeup(&ticks)，从而唤醒本进程。
 * 返回：成功 0；n < 0 或等待期间被 kill 时返回 -1。
 */
uint64
sys_pause(void)
{
  int n;

  argint(0, &n);
  if (n < 0)
    return (uint64)-1;
  if (n == 0)
    return 0;

  acquire(&tickslock);
  uint64 start = ticks;
  while ((int)(ticks - start) < n) {
    if (myproc()->killed) {
      release(&tickslock);
      return (uint64)-1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

/*
 * kill(pid)（SYS_kill = 6，lab4 可选接口）
 * 只设置目标进程的 killed 标志；若目标正在 SLEEPING，则将其改为
 * RUNNABLE，让它有机会执行退出路径。这里不直接释放资源，ZOMBIE
 * 仍由父进程 wait() 回收。
 * 返回：成功 0；pid 非法或目标不存在/已退出时返回 -1。
 */
uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  if (pid < 1)
    return (uint64)-1;

  for (int i = 0; i < NPROC; i++) {
    struct proc *p = &procs[i];

    acquire(&p->lock);
    if (p->pid == pid && p->state != UNUSED && p->state != ZOMBIE) {
      p->killed = 1;
      if (p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return (uint64)-1;
}

/*
 * schedstat(pid, &cpu_ticks, &last_slice_ticks)（SYS_schedstat = 23）
 * 参数：pid = -1 表示当前进程；后两个参数是用户态 int* 输出地址。
 * 实现：持目标进程 p->lock，把统计值快照到局部变量，释放锁后再
 * copyout 到用户空间，避免持自旋锁访问用户页。
 * 返回：成功 0；目标不存在/已退出或 copyout 失败时返回 -1。
 */
uint64
sys_schedstat(void)
{
  int pid, cpu = 0, slice = 0, found = 0;
  uint64 cpuaddr, sliceaddr;
  struct proc *caller = myproc();

  argint(0, &pid);
  argaddr(1, &cpuaddr);
  argaddr(2, &sliceaddr);

  if (pid == -1) {
    struct proc *p = caller;

    acquire(&p->lock);
    if (p->state != UNUSED && p->state != ZOMBIE) {
      cpu = p->cpu_ticks;
      slice = p->last_slice_ticks;
      found = 1;
    }
    release(&p->lock);
  } else {
    for (int i = 0; i < NPROC; i++) {
      struct proc *p = &procs[i];

      acquire(&p->lock);
      if (p->pid == pid && p->state != UNUSED && p->state != ZOMBIE) {
        cpu = p->cpu_ticks;
        slice = p->last_slice_ticks;
        found = 1;
      }
      release(&p->lock);
      if (found)
        break;
    }
  }

  /* 先完成统计值快照并释放 p->lock，再写入用户地址。 */
  if (!found)
    return (uint64)-1;
  if (copyout(caller->pagetable, caller->sz, cpuaddr,
              (char *)&cpu, sizeof(cpu)) != 0)
    return (uint64)-1;
  if (copyout(caller->pagetable, caller->sz, sliceaddr,
              (char *)&slice, sizeof(slice)) != 0)
    return (uint64)-1;
  return 0;
}

/*
 * time_rdtime()（SYS_time_rdtime = 24）
 * 直接读取 RISC-V 的 time CSR，返回硬件计时器计数，不是内核 tick 数。
 */
uint64
sys_time_rdtime(void)
{
  return r_time();
}

/*
 * setickets(n)（SYS_setickets = 25）
 * 设置当前进程的票数，并置 tickets_override，覆盖按程序名派生的默认票数。
 * 步长调度在 pick_proc() 中用该票数计算 stride = 1000000 / tickets。
 * 返回：成功 0；n < 1 时返回 -1。
 */
uint64
sys_setickets(void)
{
  int tickets;
  struct proc *p = myproc();

  argint(0, &tickets);
  if (tickets < 1)
    return (uint64)-1;

  acquire(&p->lock);
  p->tickets = tickets;
  p->tickets_override = 1;
  release(&p->lock);
  return 0;
}

uint64
sys_write(void)
{
  struct proc *p = myproc();
  int fd, n, done = 0;
  uint64 buf;
  char kbuf[256];

  argint(0, &fd);
  argaddr(1, &buf);
  argint(2, &n);

  if (fd < 0 || n < 0 || n > 4096)
    return (uint64)-1;

  while (done < n) {
    int chunk = n - done;

    if (chunk > (int)sizeof(kbuf))
      chunk = (int)sizeof(kbuf);
    if (copyin(p->pagetable, p->sz, kbuf, buf + done, chunk) != 0)
      return (uint64)-1;
    for (int i = 0; i < chunk; i++)
      consputc((uchar)kbuf[i]);
    done += chunk;
  }
  return (uint64)done;
}

uint64
sys_read(void)
{
  struct proc *p = myproc();
  int fd, n, total = 0;
  uint64 buf;
  char kbuf[256];

  argint(0, &fd);
  argaddr(1, &buf);
  argint(2, &n);

  if (fd != 0 || n < 0 || n > 4096)
    return (uint64)-1;

  while (total < n) {
    int want = n - total;
    int got;

    if (want > (int)sizeof(kbuf))
      want = (int)sizeof(kbuf);
    got = consoleread(kbuf, want);
    if (got < 0)
      return (uint64)-1;
    if (got == 0)
      break;
    if (copyout(p->pagetable, p->sz, buf + total, kbuf, got) != 0)
      return (uint64)-1;
    total += got;
    if (got < want)
      break;
  }
  return (uint64)total;
}
