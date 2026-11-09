#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "proc.h"
#include "syscall.h"
#include "defs.h"

#define NELEM(x) (sizeof(x) / sizeof((x)[0]))

static uint64
argraw(int n)
{
  /* 系统调用参数来自保存用户寄存器的 trapframe：a0..a5。 */
  struct trapframe *tf = myproc()->trapframe;

  switch (n) {
  case 0: return tf->a0;
  case 1: return tf->a1;
  case 2: return tf->a2;
  case 3: return tf->a3;
  case 4: return tf->a4;
  case 5: return tf->a5;
  }
  return 0;
}

void
argint(int n, int *ip)
{
  /* 按有符号整数解释第 n 个系统调用参数。 */
  *ip = (int)argraw(n);
}

void
argaddr(int n, uint64 *ip)
{
  /* 按用户态虚拟地址解释第 n 个系统调用参数。 */
  *ip = argraw(n);
}

int
argstr(int n, char *buf, int max)
{
  struct proc *p = myproc();
  uint64 addr;

  argaddr(n, &addr);
  if (addr == 0 || max <= 0)
    return -1;
  if (copyinstr(p->pagetable, p->sz, buf, addr, max) < 0)
    return -1;
  return strlen(buf);
}

extern uint64 sys_fork(void);
extern uint64 sys_exit(void);
extern uint64 sys_wait(void);
extern uint64 sys_read(void);
extern uint64 sys_exec(void);
extern uint64 sys_getpid(void);
extern uint64 sys_uptime(void);
extern uint64 sys_write(void);
extern uint64 sys_sbrk(void);

/* lab4 新增/补全系统调用的内核入口，函数体定义在 kernel/sysproc.c。 */
extern uint64 sys_pause(void);
extern uint64 sys_kill(void);
extern uint64 sys_schedstat(void);
extern uint64 sys_time_rdtime(void);
extern uint64 sys_setickets(void);

/* 系统调用分发表：数组下标就是系统调用号，元素是对应的内核实现。 */
static uint64 (*syscalls[])(void) = {
  [SYS_fork]   sys_fork,
  [SYS_exit]   sys_exit,
  [SYS_wait]   sys_wait,
  [SYS_read]   sys_read,
  [SYS_exec]   sys_exec,
  [SYS_getpid] sys_getpid,
  [SYS_sbrk]   sys_sbrk,
  [SYS_uptime] sys_uptime,
  [SYS_write]  sys_write,
  [SYS_pause]       sys_pause,       /* 按 tick 休眠 n 个时钟中断 */
  [SYS_kill]        sys_kill,        /* 可选：标记并唤醒目标进程 */
  [SYS_schedstat]   sys_schedstat,   /* 输出 CPU tick 与本次时间片 */
  [SYS_time_rdtime] sys_time_rdtime, /* 输出硬件 time CSR 计数 */
  [SYS_setickets]   sys_setickets,   /* 设置当前进程票数 */
};

/*
 * 系统调用总入口。
 * 跳转链路：用户态 usys.S 桩把调用号放入 a7 并执行 ecall；
 * usertrap() 识别 ecall（scause == 8）后调用本函数；
 * 本函数以 a7 为下标查 syscalls[]，间接调用对应的 sys_xxx()；
 * 最后把返回值写回 trapframe->a0，作为用户态系统调用的返回值。
 */
void
syscall(void)
{
  struct proc *p = myproc();
  int num = (int)p->trapframe->a7;
  uint64 ret;

  /* 先校验调用号，再从分发表间接跳转到具体实现。 */
  if (num > 0 && num < (int)NELEM(syscalls) && syscalls[num] != 0)
    ret = syscalls[num]();
  else
    ret = (uint64)-1;

  /* wait/exit 等调用可能已经切换到其他进程，只有仍是原进程时才写回 a0。 */
  if (myproc() == p)
    p->trapframe->a0 = ret;
}
