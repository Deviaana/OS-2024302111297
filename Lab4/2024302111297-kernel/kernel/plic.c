#include "types.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

/* 中断使能链路的中间一环:PLIC(平台级中断控制器)。 */

void
plicinit(void)
{
  /* 优先级 0 等于屏蔽,所以要把用到的 IRQ 置成非 0 */
  *(uint32 *)(PLIC_PRIORITY + UART0_IRQ * 4) = 1;
}

void
plicinithart(void)
{
  int hart = cpuid();

  /* 打开本 hart 的 S 态 IRQ 使能位 */
  *(uint32 *)PLIC_SENABLE(hart) = (1 << UART0_IRQ);
  /* 优先级阈值 0:不屏蔽任何优先级的 IRQ */
  *(uint32 *)PLIC_SPRIORITY(hart) = 0;
}

int
plic_claim(void)
{
  return *(uint32 *)PLIC_SCLAIM(cpuid());
}

void
plic_complete(int irq)
{
  *(uint32 *)PLIC_SCLAIM(cpuid()) = irq;
}
