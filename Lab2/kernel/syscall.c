#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "proc.h"
#include "syscall.h"
#include "defs.h"

#define NELEM(x) (sizeof(x) / sizeof((x)[0]))

/* ---- 参数解包接口 ----
 * 陷入时 uservec 已把用户寄存器存进现场页,所以参数就是现场页里的
 * a0/a1/a2...。命名与 xv6 一致,方便对照,但实现是本实验自己的。*/
uint64
argraw(int n)
{
  switch (n) {
  case 0: return UTRAPFRAME->a0;
  case 1: return UTRAPFRAME->a1;
  case 2: return UTRAPFRAME->a2;
  case 3: return UTRAPFRAME->a3;
  case 4: return UTRAPFRAME->a4;
  case 5: return UTRAPFRAME->a5;
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

/* 取第 n 个参数当作用户字符串拷进内核缓冲。
 * lab2 没有页表也没有合法性检查:用户指针直接解引用。
 * 这笔债记在账上,lab3 上 sv39 + copyin/copyinstr 时还。*/
int
argstr(int n, char *buf, int max)
{
  uint64 addr;
  int i;

  argaddr(n, &addr);
  if (addr == 0)
    return -1;

  const char *s = (const char *)addr;

  for (i = 0; i < max - 1 && s[i]; i++)
    buf[i] = s[i];
  buf[i] = 0;
  return i;
}

/* ---- 系统调用实现(在 proc.c / sysproc.c 里)---- */
extern uint64 sys_fork(void);
extern uint64 sys_exit(void);
extern uint64 sys_wait(void);
extern uint64 sys_read(void);
extern uint64 sys_exec(void);
extern uint64 sys_getpid(void);
extern uint64 sys_uptime(void);
extern uint64 sys_write(void);

/* 调用号表即合同(见 gifts/kernel/syscall.h 与 gifts/user/usys.pl):
 * 编号 1..22 由课程规定,不许自创。表里为 0 的号 = 未实现。
 * 采用表驱动而不是 switch:加一个调用只动一行,且天然没有"漏 default"。*/
static uint64 (*syscalls[])(void) = {
  [SYS_fork]   sys_fork,
  [SYS_exit]   sys_exit,
  [SYS_wait]   sys_wait,
  [SYS_read]   sys_read,
  [SYS_exec]   sys_exec,
  [SYS_getpid] sys_getpid,
  [SYS_uptime] sys_uptime,
  [SYS_write]  sys_write,
};

void
syscall(void)
{
  struct proc *p = myproc();
  int num = (int)UTRAPFRAME->a7;
  uint64 ret;

  if (num > 0 && num < (int)NELEM(syscalls) && syscalls[num] != 0)
    ret = syscalls[num]();
  else {
    /* 未知/未实现的调用号:返回确定的 -1,绝不 panic(badecall 用例) */
    ret = (uint64)-1;
  }

  /* 只有"没发生进程切换"时才回填 a0。
   * fork/wait/exit 会把现场页换成另一个进程的,回填就踩掉对方的 a0。*/
  if (myproc() == p)
    UTRAPFRAME->a0 = ret;
}