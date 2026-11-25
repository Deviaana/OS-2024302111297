#include "types.h"
#include "course_sid.h"
#include "defs.h"

void
main(void)
{
  consoleinit();
  console_banner();

  kinit();
  kref_selftest();
  procinit();
  kvminit();
  kvminithart();

  plicinit();
  plicinithart();
  trapinit();
  trapinithart();

  userinit();
  scheduler();
}
