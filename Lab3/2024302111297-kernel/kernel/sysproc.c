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
