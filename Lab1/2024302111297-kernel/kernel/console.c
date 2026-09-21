#include "types.h"
#include "memlayout.h"
#include "course_sid.h"

#define UART_THR 0
#define UART_LSR 5
#define UART_LSR_THRE 0x20

static uint64 emitted;

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

void
consoleinit(void)
{
  emitted = 0;
}

void
consputc(int c)
{
  uartputc_raw(c);
  emitted++;
  throttle_if_needed();

#if LAB1_BANNER_PROTOCOL == 1
  uartputc_raw('.');
  emitted++;
  throttle_if_needed();
#endif
}
