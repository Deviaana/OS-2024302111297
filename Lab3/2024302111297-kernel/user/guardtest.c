// Self-test: write to the guard page immediately below the current user stack.
#include "kernel/types.h"
#include "user/user.h"

#define PGSIZE 4096

int
main(void)
{
  int pid = fork();

  if (pid == 0) {
    uint64 sp;

    asm volatile("mv %0, sp" : "=r"(sp));
    volatile char *guard = (volatile char *)((sp & ~(PGSIZE - 1)) - PGSIZE);
    *guard = 0x5a;
    printf("GUARDTEST FAIL: stack guard write survived\n");
    exit(0);
  }

  int st = 0;
  wait(&st);
  if (st != 0) {
    printf("GUARDTEST PASS: stack guard write killed (status=%d)\n", st);
    exit(0);
  }
  printf("GUARDTEST FAIL: child exited normally after guard write\n");
  exit(1);
}
