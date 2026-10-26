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
  *ip = (int)argraw(n);
}

void
argaddr(int n, uint64 *ip)
{
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
};

void
syscall(void)
{
  struct proc *p = myproc();
  int num = (int)p->trapframe->a7;
  uint64 ret;

  if (num > 0 && num < (int)NELEM(syscalls) && syscalls[num] != 0)
    ret = syscalls[num]();
  else
    ret = (uint64)-1;

  /* A call such as wait/exit may have switched to another process. */
  if (myproc() == p)
    p->trapframe->a0 = ret;
}
