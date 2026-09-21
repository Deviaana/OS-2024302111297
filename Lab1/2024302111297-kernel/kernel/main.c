#include "types.h"
#include "course_sid.h"

#define STR1(x) #x
#define STR(x) STR1(x)
#define COURSE_SID_STR STR(COURSE_SID)

void consoleinit(void);
void printf(const char *, ...);

void
main(void)
{
  consoleinit();

  printf("OSLAB1 sid=%s mod97=0x%x\n", COURSE_SID_STR,
         (uint)(COURSE_SID % 97));

  for (;;)
    asm volatile("wfi");
}
