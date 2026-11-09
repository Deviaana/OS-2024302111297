// Self-test: a write() source buffer crossing a user-page boundary.
#include "kernel/types.h"
#include "user/user.h"

#define PGSIZE 4096

int
main(void)
{
  char *p = sbrk(2 * PGSIZE);
  char pattern[8] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'};

  if (p == SBRK_ERROR) {
    printf("COPYTEST FAIL: sbrk\n");
    exit(1);
  }

  for (int i = 0; i < (int)sizeof(pattern); i++)
    p[PGSIZE - 4 + i] = pattern[i];

  for (int i = 0; i < (int)sizeof(pattern); i++) {
    if (p[PGSIZE - 4 + i] != pattern[i]) {
      printf("COPYTEST FAIL: user buffer mismatch\n");
      exit(1);
    }
  }

  printf("COPYTEST output: ");
  write(1, p + PGSIZE - 4, sizeof(pattern));
  printf("\nCOPYTEST PASS: cross-page copyin completed\n");
  exit(0);
}
