#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "proc.h"
#include "syscall.h"
#include "defs.h"

/* 只实现 sh/hi/spin 与官方测试用得到的调用;其余号在分发处统一回 -1。 */

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
  int fd, n;
  uint64 buf;

  argint(0, &fd);
  argaddr(1, &buf);
  argint(2, &n);

  if (fd < 0 || n < 0 || n > 4096)
    return (uint64)-1;

  /* lab2 没有文件系统:所有写都落到控制台。
   * 注意走的是 consputc(明文),不是 banner 协议通道,否则
   * 用户程序的每个字节后面都会多一个 '.'。*/
  char *s = (char *)buf;      /* 用户指针直接解引用(债:lab3) */
  for (int i = 0; i < n; i++)
    consputc(s[i]);

  return (uint64)n;
}

uint64
sys_read(void)
{
  int fd, n;
  uint64 buf;

  argint(0, &fd);
  argaddr(1, &buf);
  argint(2, &n);

  /* 现阶段唯一的输入源是键盘(标准输入 fd=0) */
  if (fd != 0 || n < 0 || n > 4096)
    return (uint64)-1;

  return (uint64)consoleread(buf, n);
}