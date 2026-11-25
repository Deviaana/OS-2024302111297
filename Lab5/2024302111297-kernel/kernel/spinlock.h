#ifndef SPINLOCK_H
#define SPINLOCK_H

#include "types.h"

struct cpu;

struct spinlock {
  uint locked;
  struct cpu *cpu;
  char *name;
};

void initlock(struct spinlock *lk, char *name);
void acquire(struct spinlock *lk);
void release(struct spinlock *lk);
int holding(struct spinlock *lk);

#endif
