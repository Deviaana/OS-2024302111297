#include "types.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

void
initlock(struct spinlock *lk, char *name)
{
  lk->locked = 0;
  lk->cpu = 0;
  lk->name = name;
}

void
acquire(struct spinlock *lk)
{
  push_off();

  if (holding(lk)) {
    printf("acquire: %s\n", lk->name);
    panic("acquire");
  }

  while (__sync_lock_test_and_set(&lk->locked, 1) != 0)
    ;

  __sync_synchronize();
  lk->cpu = mycpu();
}

void
release(struct spinlock *lk)
{
  if (!holding(lk))
    panic("release");

  lk->cpu = 0;
  __sync_synchronize();
  __sync_lock_release(&lk->locked);
  pop_off();
}

int
holding(struct spinlock *lk)
{
  return lk->locked && lk->cpu == mycpu();
}
