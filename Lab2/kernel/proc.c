#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "proc.h"
#include "syscall.h"
#include "defs.h"

/* ===== 进程表 =====
 * lab2 还没有 kalloc/页表,所以进程槽是"编译期就分好的一块块物理内存":
 * 第 i 个进程独占 [USERIMG_BASE + i*USERIMG_SLOT, +64KB),里头放
 * 平铺二进制(槽底)和用户栈(槽顶)。谁记账?就是这张表 + p->img。*/
struct proc procs[NPROC];

static struct proc *current;
static int nextpid = 2;          /* pid 1 留给 sh */

struct proc *
myproc(void)
{
  return current;
}

/* ===== 内嵌程序表 ABI(见 gifts/Makefile.upgrade 注释)=====
 *   _uprog_table: {u64 start; u64 end; char name[]} 依次排列,{0,0} 结尾
 *   _uprog_entry: 与 _uprog_table 同序的入口偏移表
 *
 * 为什么要第二张表:用户程序按 user.ld 从地址 0 链接,加载到槽底后
 * 代码是 PC 相对的,可以原址运行;但"入口在二进制里的偏移"未必是 0 ——
 * 官方用例 badecall.c 的 main 前面还有静态函数 ecall_raw。构建期用
 * nm 从 ELF 里取 main 的地址生成这张表(见 Makefile 的 userentry 规则),
 * 不改动任何赠送件。*/
struct uprog {
  uint64 start;
  uint64 end;
  char name[];
};

extern char _uprog_table[];
extern uint64 _uprog_entry[];

static int
prog_lookup(const char *name, uint64 *start, uint64 *size, uint64 *entry)
{
  char *p = _uprog_table;
  int idx = 0;

  for (;;) {
    struct uprog *u = (struct uprog *)p;

    if (u->start == 0 && u->end == 0)
      return -1;
    if (strcmp(u->name, name) == 0) {
      *start = u->start;
      *size = u->end - u->start;
      *entry = _uprog_entry[idx];
      return 0;
    }
    p += 2 * sizeof(uint64) + strlen(u->name) + 1;
    p = (char *)(((uint64)p + 7) & ~7ULL);   /* 表项 .balign 8 */
    idx++;
  }
}

/* 装载内嵌程序,并把"第一现场"直接写进现场页 */
static int
prog_load(struct proc *p, const char *name)
{
  uint64 start, size, entry;
  struct trapframe *tf = UTRAPFRAME;

  if (prog_lookup(name, &start, &size, &entry) < 0)
    return -1;
  if (size == 0 || size > USERIMG_SLOT - 4096)   /* 给用户栈留位置 */
    return -1;

  memmove(p->img, (void *)start, (int)size);
  p->imgsize = size;

  memset(tf, 0, sizeof(*tf));
  tf->epc = (uint64)p->img + entry;
  tf->sp = (uint64)p->img + USERIMG_SLOT - 16;   /* 栈向下长,顶必须 16 对齐 */
  return 0;
}

static struct proc *
proc_alloc(void)
{
  for (int i = 1; i < NPROC; i++) {
    if (procs[i].state == UNUSED) {
      struct proc *p = &procs[i];

      p->state = RUNNABLE;
      p->pid = nextpid++;
      p->parent = -1;
      p->xstatus = 0;
      p->imgsize = 0;
      p->img = (char *)(USERIMG_BASE + (uint64)i * USERIMG_SLOT);
      return p;
    }
  }
  return 0;
}

/* 进程切换。现场页是全局一份(trampoline.S 用常量 TRAPFRAME 寻址),
 * 所以"切进程"就是把它存回旧的、再从新的装回来。*/
static void
proc_switch(struct proc *from, struct proc *to)
{
  memmove(&from->tf, UTRAPFRAME, sizeof(struct trapframe));
  memmove(UTRAPFRAME, &to->tf, sizeof(struct trapframe));
  current = to;
}

/* 父进程槽里的绝对地址 → 子进程槽里的对应地址。
 * 没有页表时两个进程的镜像不在同一物理地址,epc/sp/fp 以及任何指向
 * 镜像或用户栈的寄存器都得跟着搬,否则子进程会跑到父进程身上去。
 * 局限:值恰好落在 64KB 槽窗口内的普通整数会被误搬(记为 lab2 的债,
 * lab3 给每个进程同一套虚拟地址后自然消失)。*/
static void
tf_relocate(struct trapframe *f, uint64 from, uint64 to)
{
  uint64 *regs[] = {
    &f->epc, &f->ra, &f->sp, &f->gp, &f->tp,
    &f->t0, &f->t1, &f->t2, &f->s0, &f->s1,
    &f->a0, &f->a1, &f->a2, &f->a3, &f->a4, &f->a5, &f->a6, &f->a7,
    &f->s2, &f->s3, &f->s4, &f->s5, &f->s6, &f->s7, &f->s8, &f->s9,
    &f->s10, &f->s11, &f->t3, &f->t4, &f->t5, &f->t6,
  };
  uint n = (uint)(sizeof(regs) / sizeof(regs[0]));

  for (uint i = 0; i < n; i++) {
    uint64 v = *regs[i];

    if (v >= from && v < from + USERIMG_SLOT)
      *regs[i] = to + (v - from);
  }
}

/* 第一个用户进程:内核主执行流直接"变成" sh(说明书允许的最简方案,
 * 见阶段四"启动引导选型")。*/
void
userinit(void)
{
  struct proc *p = &procs[0];

  current = p;
  p->state = RUNNING;
  p->pid = 1;
  p->parent = -1;
  p->xstatus = 0;
  p->img = (char *)USERIMG_BASE;

  if (prog_load(p, "sh") < 0)
    panic("userinit: cannot load sh from _uprog_table");

  usertrapret();   /* 不返回:一次 sret 落到 sh 的第一条指令 */
}

uint64
sys_fork(void)
{
  struct proc *p = myproc();
  struct proc *c = proc_alloc();

  if (c == 0)
    return (uint64)-1;

  c->parent = (int)(p - procs);

  /* 复制用户程序镜像与用户栈:无页表时代最直接的"复制地址空间" */
  memmove(c->img, p->img, USERIMG_SLOT);
  c->imgsize = p->imgsize;

  /* 复制现场:子进程从同一条 ecall 之后继续跑 */
  memmove(&c->tf, UTRAPFRAME, sizeof(struct trapframe));
  tf_relocate(&c->tf, (uint64)p->img, (uint64)c->img);
  c->tf.a0 = 0;                  /* 子进程里 fork() 返回 0 */

  return (uint64)c->pid;
}

uint64
sys_wait(void)
{
  struct proc *p = myproc();
  int me = (int)(p - procs);

  /* 1) 已有僵尸子进程:直接回收 */
  for (int i = 0; i < NPROC; i++) {
    struct proc *c = &procs[i];

    if (c->parent == me && c->state == ZOMBIE) {
      int pid = c->pid;

      c->state = UNUSED;
      c->pid = 0;
      c->parent = -1;
      return (uint64)pid;
    }
  }

  /* 2) 有就绪子进程:lab2 的"假调度"——把控制权直接交给它,
   *    它 exit 时会带着我的 wait 返回值切回我这里(见 sys_exit)。*/
  for (int i = 0; i < NPROC; i++) {
    struct proc *c = &procs[i];

    if (c->parent == me && c->state == RUNNABLE) {
      p->state = BLOCKED;
      c->state = RUNNING;
      proc_switch(p, c);
      return 0;                  /* 返回值由子进程 exit 时填好 */
    }
  }

  return (uint64)-1;             /* 没有子进程 */
}

uint64
sys_exit(void)
{
  struct proc *p = myproc();
  int status;

  argint(0, &status);

  p->state = ZOMBIE;
  p->xstatus = status;
  p->imgsize = 0;

  if (p->parent < 0) {
    /* 没有父进程(sh 自己退出):停机 */
    printf("proc %d exit with no parent\n", p->pid);
    halt();
  }

  struct proc *par = &procs[p->parent];

  if (par->state == BLOCKED) {
    /* 正常路径。lab2 的"假调度"保证:子进程只可能在父进程 wait() 期间
     * 才拿得到 CPU,所以此刻父进程一定正卡在 wait() 里等它。
     * 于是 pid 直接写进父进程的 a0 就等于"交付 exit 状态",子进程槽
     * 可以立刻回收 —— 否则下一次 wait() 会先捞到这个旧僵尸、
     * 直接返回它的 pid,而真正的新子进程永远排不上(实测会卡住 sh)。*/
    par->tf.a0 = (uint64)p->pid;   /* 父进程 wait() 的返回值 */
    par->state = RUNNING;

    p->state = UNUSED;
    p->parent = -1;
    p->pid = 0;

    proc_switch(p, par);
    /* proc_switch 只是把现场页换成了父进程的;真正"切过去"发生在 syscall()
     * 返回之后的 usertrapret()。所以这里必须立刻返回 —— 否则会以刚被回收的
     * 子进程身份落到下面的 printf/halt,整机停死(实测如此)。*/
    return 0;
  }

  /* 父进程没在等(当前模型下不可达):留成僵尸,等它将来 wait 时回收 */
  printf("proc %d exit: parent %d is not waiting\n", p->pid, p->parent);
  halt();
  return 0;
}

uint64
sys_exec(void)
{
  char name[64];
  struct proc *p = myproc();

  if (argstr(0, name, sizeof(name)) < 0)
    return (uint64)-1;

  /* sh 用 gets() 取整行,交过来的名字带着行尾的 \n、还可能带首尾空格,
   * 而内嵌表里存的是裸名字。lab2 忽略 argv,所以这里只取"第一个词",
   * 与 lab5 完整实现里解析 argv[0] 等价;不这么做就永远查不到表。*/
  int i = 0, j = 0;
  while (name[i] == ' ' || name[i] == '\t') i++;
  while (name[i] && name[i] != ' ' && name[i] != '\t' &&
         name[i] != '\n' && name[i] != '\r')
    name[j++] = name[i++];
  name[j] = 0;

  /* lab2 忽略 argv:内嵌程序都不带参数(完整化是 lab5 的账)。
   * 失败时返回 -1,sh 会打出 "exec xxx failed" 并继续活着。*/
  if (prog_load(p, name) < 0)
    return (uint64)-1;

  return 0;
}