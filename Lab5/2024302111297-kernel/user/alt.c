#include "kernel/types.h"
#include "user/user.h"

static void
run(char c)
{
  for (int i = 0; i < 12; i++) {
    for (volatile int d = 0; d < 20000000; d++)
      ;
    write(1, &c, 1);
  }
  exit(0);
}

int
main(void)
{
  if (fork() == 0)
    run('A');
  if (fork() == 0)
    run('B');
  wait(0);
  wait(0);
  printf("\nalt done\n");
  exit(0);
}
