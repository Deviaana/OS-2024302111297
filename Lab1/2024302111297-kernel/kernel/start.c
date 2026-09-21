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

  asm volatile("mret");
}
