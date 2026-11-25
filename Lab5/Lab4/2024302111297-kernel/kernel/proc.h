/* Per-process state for the lab3 SV39 kernel. */
#ifndef PROC_H
#define PROC_H

#include "types.h"
#include "param.h"
#include "riscv.h"

/* The layout is hard-coded in trampoline.S; do not reorder fields. */
struct trapframe {
  uint64 kernel_satp;    /*   0 */
  uint64 kernel_sp;      /*   8 */
  uint64 kernel_trap;    /*  16 */
  uint64 epc;            /*  24 */
  uint64 kernel_hartid;  /*  32 */
  uint64 ra;             /*  40 */
  uint64 sp;             /*  48 */
  uint64 gp;             /*  56 */
  uint64 tp;             /*  64 */
  uint64 t0;             /*  72 */
  uint64 t1;             /*  80 */
  uint64 t2;             /*  88 */
  uint64 s0;             /*  96 */
  uint64 s1;             /* 104 */
  uint64 a0;             /* 112 */
  uint64 a1;             /* 120 */
  uint64 a2;             /* 128 */
  uint64 a3;             /* 136 */
  uint64 a4;             /* 144 */
  uint64 a5;             /* 152 */
  uint64 a6;             /* 160 */
  uint64 a7;             /* 168 */
  uint64 s2;             /* 176 */
  uint64 s3;             /* 184 */
  uint64 s4;             /* 192 */
  uint64 s5;             /* 200 */
  uint64 s6;             /* 208 */
  uint64 s7;             /* 216 */
  uint64 s8;             /* 224 */
  uint64 s9;             /* 232 */
  uint64 s10;            /* 240 */
  uint64 s11;            /* 248 */
  uint64 t3;             /* 256 */
  uint64 t4;             /* 264 */
  uint64 t5;             /* 272 */
  uint64 t6;             /* 280 */
};

enum procstate {
  UNUSED = 0,
  RUNNABLE,
  RUNNING,
  BLOCKED,
  ZOMBIE,
};

struct proc {
  enum procstate state;
  int pid;
  int parent;              /* procs[] index, -1 for none */
  int xstatus;

  pagetable_t pagetable;   /* independent user address space */
  struct trapframe *trapframe;
  uint64 kstack;           /* virtual base of this process's kernel stack */

  uint64 sz;               /* current heap end (also copied user range) */
  uint64 heap_start;
  uint64 stack_top;
};

#endif
