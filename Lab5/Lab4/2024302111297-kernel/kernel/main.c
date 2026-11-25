#include "types.h"
#include "course_sid.h"
#include "defs.h"

void
main(void)
{
  consoleinit();
  console_banner();

  kinit();
  kvminit();
  kvminithart();

  plicinit();
  plicinithart();
  trapinithart();

  userinit();

  for (;;)
    asm volatile("wfi");
}
