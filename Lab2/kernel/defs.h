/* lab2 内核全局声明(替代 xv6 的 defs.h) */
#ifndef DEFS_H
#define DEFS_H

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "course_sid.h"

struct trapframe;
struct proc;

/* ---- printf.c ---- */
void printf(const char *fmt, ...);

/* ---- console.c ---- */
void consoleinit(void);
void consputc(int c);              /* 正文输出(内核诊断/用户 write/回显) */
void console_banner(void);         /* lab1 输出协议下的启动 banner */
void uartintr(void);               /* UART 接收中断服务例程 */
int  consoleread(uint64 dst, int n);

/* ---- trap.c ---- */
void usertrap(void);
void usertrapret(void) __attribute__((noreturn));
void trapinithart(void);
uint64 uptime(void);
void panic(const char *msg) __attribute__((noreturn));
void halt(void) __attribute__((noreturn));

/* ---- plic.c ---- */
void plicinit(void);
void plicinithart(void);
int  plic_claim(void);
void plic_complete(int irq);

/* ---- proc.c ---- */
void userinit(void);

/* ---- syscall.c ---- */
void syscall(void);
void argint(int n, int *ip);
void argaddr(int n, uint64 *ip);
int  argstr(int n, char *buf, int max);

/* ---- string.c ---- */
void *memmove(void *dst, const void *src, int n);
void *memset(void *dst, int c, uint n);
int   strlen(const char *s);
int   strcmp(const char *p, const char *q);

/* 内核栈顶。entry.S 里开辟的 boot_stack 是全部进程共用的内核栈:
 * lab2 在 S 态始终关中断(sstatus.SIE=0),不会嵌套陷入,一份足够。 */
extern char boot_stack[];
#define KSTACK_TOP ((uint64)boot_stack + LAB1_STACK_KB * 1024)

/* 现场页。uservec/userret 用常量 TRAPFRAME 寻址,所以它是"全局一份、
 * 按进程换内容":此刻它属于 myproc()。 */
#define UTRAPFRAME ((struct trapframe *)TRAPFRAME)

#endif