/* lab2 进程与现场(trapframe) */
#ifndef PROC_H
#define PROC_H

#include "types.h"
#include "param.h"

/* ===== 现场保存区 =====
 * 布局必须与 gifts/kernel/trampoline.S 里硬编码的偏移严格一一对应:
 *    0 kernel_satp   8 kernel_sp    16 kernel_trap   24 epc
 *   32 kernel_hartid
 *   40 ra   48 sp   56 gp   64 tp   72 t0   80 t1   88 t2
 *   96 s0  104 s1  112 a0  120 a1  128 a2  136 a3  144 a4  152 a5
 *  160 a6  168 a7  176 s2  184 s3  192 s4  200 s5  208 s6  216 s7
 *  224 s8  232 s9  240 s10 248 s11 256 t3  264 t4  272 t5  280 t6
 * 共 288 字节。这里少一个字段或顺序错一格,userret 就会带着错位的
 * 寄存器回到用户态 —— 现场瞬间瓦解。
 */
struct trapframe {
  uint64 kernel_satp;    /*   0 陷入后要换上的内核页表(lab2 恒为 0 = bare) */
  uint64 kernel_sp;      /*   8 内核栈顶 */
  uint64 kernel_trap;    /*  16 usertrap 入口地址 */
  uint64 epc;            /*  24 用户态下一条指令 */
  uint64 kernel_hartid;  /*  32 hartid */
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
  UNUSED = 0,   /* 空槽 */
  RUNNABLE,     /* 已就绪,等待被"假调度"推上去 */
  RUNNING,      /* 正在 U 态跑 */
  BLOCKED,      /* 卡在 wait() 里等子进程 */
  ZOMBIE,       /* 已 exit,等父进程 wait 回收 */
};

struct proc {
  enum procstate state;
  int pid;
  int parent;            /* 父进程在 procs[] 中的下标;-1 = 无父 */
  int xstatus;           /* exit(status) 留下的退出码 */
  struct trapframe tf;   /* 本进程的现场副本(切换时与现场页互拷) */
  char *img;             /* 用户镜像槽基址 */
  uint64 imgsize;        /* 已装载的程序字节数 */
};

struct proc *myproc(void);

#endif