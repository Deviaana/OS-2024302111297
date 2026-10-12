#include "types.h"
#include "course_sid.h"
#include "defs.h"

void
main(void)
{
  consoleinit();      /* 串口初始化 + 打开接收中断(IER) */

  /* lab1 输出通道回归:启动 banner 照旧按 LAB1_BANNER_PROTOCOL 打出 */
  console_banner();

  plicinit();         /* PLIC:UART0 的 IRQ 优先级 */
  plicinithart();     /* PLIC:本 hart 的 S 态使能位与阈值 */
  trapinithart();     /* stvec = uservec;放行 sie;装上第一次时钟 */

  /* 内核主执行流直接变成 sh:填好第一现场后 sret 进 U 态(不返回) */
  userinit();

  for (;;)
    asm volatile("wfi");
}