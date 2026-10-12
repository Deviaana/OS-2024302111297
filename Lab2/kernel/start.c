#include "types.h"
#include "riscv.h"

extern void main(void);
extern void machine_trap_vector(void);

void
start(void)
{
  uint64 x = r_mstatus();
  x &= ~(1L << 3);
  x &= ~MSTATUS_MPP_MASK;
  x |= MSTATUS_MPP_S;
  w_mstatus(x);

  w_mepc((uint64)main);
  asm volatile("csrw mtvec, %0" : : "r"((uint64)machine_trap_vector));
  w_satp(0);

  w_medeleg(0xffff);
  w_mideleg(0xffff);
  w_sie(0);

  w_pmpaddr0(0x3fffffffffffffull);
  w_pmpcfg0(0xf);

  /* ---- lab2 补充:S 态时钟中断所需的两项 M 态放行 ----
   * 1) menvcfg.STCE:打开 sstc 扩展,S 态才能读写 stimecmp(0x14d);
   * 2) mcounteren.TM:允许 S 态读 time 计数器(算下一次到期时刻)。
   *    不打开这两项,S 态访问 stimecmp/time 会直接非法指令陷入。
   */
  w_menvcfg(r_menvcfg() | MENVCFG_STCE);
  w_mcounteren(r_mcounteren() | 2);

  asm volatile("mret");
}