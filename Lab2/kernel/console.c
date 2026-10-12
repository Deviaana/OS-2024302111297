#include "types.h"
#include "memlayout.h"
#include "riscv.h"
#include "course_sid.h"
#include "defs.h"

/* UART 16550(QEMU virt 的 UART0 = 0x10000000)寄存器偏移 */
#define UART_RHR 0            /* 读:接收保持寄存器 */
#define UART_THR 0            /* 写:发送保持寄存器 */
#define UART_IER 1            /* 中断使能 */
#define UART_LSR 5            /* 线路状态 */
#define UART_LSR_DR   0x01    /* 有数据可读 */
#define UART_LSR_THRE 0x20    /* 发送保持寄存器空 */
#define UART_IER_RDI  0x01    /* 接收数据可用中断使能 */

static uint64 emitted;

/* ---------------- 输出 ---------------- */

static void
uartputc_raw(int c)
{
  volatile uchar *uart = (volatile uchar *)UART0;

  while ((uart[UART_LSR] & UART_LSR_THRE) == 0)
    ;
  uart[UART_THR] = (uchar)c;
}

static void
throttle_if_needed(void)
{
  const uint64 period = 16 + (COURSE_SID % 16);

  if (period != 0 && emitted % period == 0) {
    for (volatile uint64 i = 0; i < 1024; i++)
      asm volatile("nop");
  }
}

/* 正文输出:内核诊断(printf)、用户程序 write、键盘回显都走这里,明文 */
void
consputc(int c)
{
  uartputc_raw(c);
  emitted++;
  throttle_if_needed();
}

/* ---- lab1 输出协议回归 ----
 * LAB1_BANNER_PROTOCOL == 1:每个字节后面紧跟一个 '.'(见 expect_banner.txt)。
 * 只施加于 banner:用户程序输出与回显必须是明文,否则 sh/hi/官方测试
 * 的输出会被 '.' 污染,EXPECT 校验也会失配。 */
#if LAB1_BANNER_PROTOCOL == 1
static void
bannerputc(int c)
{
  consputc(c);
  consputc('.');
}
#else
static void
bannerputc(int c)
{
  consputc(c);
}
#endif

static void
bannerputs(const char *s)
{
  for (; *s; s++)
    bannerputc(*s);
}

static void
bannerputhex(uint64 v)
{
  char tmp[16];
  int i = 0;
  const char *digits = "0123456789abcdef";

  do {
    tmp[i++] = digits[v % 16];
    v /= 16;
  } while (v != 0);

  while (--i >= 0)
    bannerputc(tmp[i]);
}

#define STR1(x) #x
#define STR(x) STR1(x)

void
console_banner(void)
{
  bannerputs("OSLAB1 sid=");
  bannerputs(STR(COURSE_SID));
  bannerputs(" mod97=0x");
  bannerputhex(COURSE_SID % 97);
  bannerputc('\n');
}

/* ---------------- 输入:环形缓冲区 ---------------- */

/* r/w 单调递增,索引取模。w-r 的无符号差值就是当前元素个数,
 * 计数回绕也不影响判断(经典环形缓冲)。 */
static uchar buf[LAB2_BUF_SIZE];
static uint64 rpos, wpos;

/* lab2 的"唤醒"标志:中断侧在语义满足时置位,读取侧轮询它。
 * 它不是 sleep/wakeup,只是把"唤醒条件"集中到一处 —— lab4 换成
 * 真正的 sleep(chan)/wakeup(chan) 时,改的正是这两个位置。 */
static volatile int cons_wakeup;

static int
buf_empty(void)
{
  return (wpos - rpos) == 0;
}

static int
buf_full(void)
{
  return (wpos - rpos) == LAB2_BUF_SIZE;
}

/* 行缓冲 / 字符流语义的**唯一**判据:中断侧与读取侧共用同一个函数。
 * 两处必须同时改变语义;只改一处(比如只在中断里改唤醒条件)就会出现
 * "读者永远等不到唤醒"或"还没到一行就返回"的不一致。见设计笔记 §3。 */
static int
buf_ready(void)
{
#if LAB2_BUF_SEMANTICS == 0
  /* 行缓冲:缓冲区里出现 '\n' 才算"一行到齐" */
  for (uint64 i = rpos; i != wpos; i++) {
    if (buf[i % LAB2_BUF_SIZE] == '\n')
      return 1;
  }
  return 0;
#else
  /* 字符流:有字符就能交付 */
  return !buf_empty();
#endif
}

static void
buf_push(int c)
{
  if (buf_full())
    return;                  /* 满则丢弃:lab2 的文档化行为(不阻塞、不报错) */

  buf[wpos % LAB2_BUF_SIZE] = (uchar)c;
  wpos++;

  if (buf_ready())
    cons_wakeup = 1;
}

static int
buf_pop(void)
{
  int c = buf[rpos % LAB2_BUF_SIZE];

  rpos++;
  return c;
}

/* 非阻塞地从 UART 取一个字符;没有数据返回 -1 */
static int
uartgetc(void)
{
  volatile uchar *uart = (volatile uchar *)UART0;

  if ((uart[UART_LSR] & UART_LSR_DR) == 0)
    return -1;
  return uart[UART_RHR];
}

/* 收到一个字符之后的处理:规范化换行、退格、入缓冲、回显。
 * 中断路径与轮询路径共用这一份,语义不会出现第二份实现。*/
static void
console_push(int c)
{
  if (c == '\r')
    c = '\n';

  if (c == 0x7f || c == '\b') {
    /* 退格:从缓冲区撤回一个字符,并擦掉屏幕上的回显 */
    if (!buf_empty()) {
      wpos--;
      consputc('\b');
      consputc(' ');
      consputc('\b');
    }
    return;
  }

  buf_push(c);
  consputc(c);               /* 回显 */
}

/* UART 接收中断服务例程:把 FIFO 里的字符全部搬进环形缓冲区 */
void
uartintr(void)
{
  int c;

  while ((c = uartgetc()) >= 0)
    console_push(c);
}

void
consoleinit(void)
{
  volatile uchar *uart = (volatile uchar *)UART0;

  emitted = 0;
  rpos = 0;
  wpos = 0;
  cons_wakeup = 0;

  /* 中断使能链路的第一环:UART 自己的接收中断使能位 */
  uart[UART_IER] = UART_IER_RDI;
}

/* read(0, dst, n):从环形缓冲区取数据。
 * lab2 没有 sleep/wakeup,用"关中断 + 轮询 UART"顶上:自己在 S 态
 * 把字符搬进缓冲,等价于"被唤醒一次"。局限与 lab4 重构见设计笔记 §3。*/
int
consoleread(uint64 dst, int n)
{
  uchar *p = (uchar *)dst;   /* 用户指针直接解引用(债:lab3 上 copyout) */
  int got = 0;

  while (got < n) {
    if (!buf_ready()) {
      if (got > 0)
        break;               /* 能交付的先交付完,返回短读 */

      /* 检查-入睡:语义还没满足 → 轮询等待 */
      cons_wakeup = 0;
      while (!cons_wakeup) {
        int c = uartgetc();

        if (c >= 0)
          console_push(c);
      }
      continue;
    }

    p[got++] = (uchar)buf_pop();

#if LAB2_BUF_SEMANTICS == 0
    if (p[got - 1] == '\n')
      break;                 /* 行缓冲:交付到行尾为止 */
#endif
  }

  return got;
}