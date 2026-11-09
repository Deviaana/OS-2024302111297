#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "proc.h"
#include "syscall.h"
#include "defs.h"

struct proc procs[NPROC];
struct cpu cpus[NCPU];

static struct proc *initproc;
static struct spinlock wait_lock;
static int nextpid = 2;
static int rr_idx = -1;
static uint64 stride_virtual;

extern char trampoline[];
extern char _uprog_table[];
extern uint64 _uprog_entry[];

struct uprog {
  uint64 start;
  uint64 end;
  uint64 text_end;
  uint64 data_start;
  uint64 file_size;
  char name[];
};

struct prog_info {
  uint64 start;
  uint64 end;
  uint64 text_end;
  uint64 data_start;
  uint64 file_size;
  uint64 entry;
};

static void forkret(void);

int
cpuid(void)
{
  return (int)r_tp();
}

struct cpu *
mycpu(void)
{
  int id = cpuid();

  if (id < 0 || id >= NCPU)
    panic("mycpu: bad hartid");
  return &cpus[id];
}

void
push_off(void)
{
  int old = intr_get();

  intr_off();
  struct cpu *c = mycpu();

  if (c->noff == 0)
    c->intena = old;
  c->noff++;
}

void
pop_off(void)
{
  struct cpu *c = mycpu();

  if (intr_get())
    panic("pop_off: interruptible");
  if (c->noff < 1)
    panic("pop_off: noff");
  c->noff--;
  if (c->noff == 0 && c->intena)
    intr_on();
}

struct proc *
myproc(void)
{
  return mycpu()->proc;
}

void
procinit(void)
{
  for (int i = 0; i < NPROC; i++) {
    initlock(&procs[i].lock, "proc");
    procs[i].state = UNUSED;
    procs[i].pid = 0;
  }
  initlock(&wait_lock, "wait");
}

static int
prog_lookup(const char *name, struct prog_info *info)
{
  char *p = _uprog_table;
  int idx = 0;

  for (;;) {
    struct uprog *u = (struct uprog *)p;

    if (u->start == 0 && u->end == 0)
      return -1;
    if (strcmp(u->name, name) == 0) {
      info->start = u->start;
      info->end = u->end;
      info->text_end = u->text_end;
      info->data_start = u->data_start;
      info->file_size = u->file_size;
      info->entry = _uprog_entry[idx];
      return 0;
    }
    p += 5 * sizeof(uint64) + strlen(u->name) + 1;
    p = (char *)(((uint64)p + 7) & ~7ULL);
    idx++;
  }
}

void
proc_mapstacks(pagetable_t pagetable)
{
  for (int i = 0; i < NPROC; i++) {
    char *pa = kalloc();

    if (pa == 0)
      panic("proc_mapstacks: out of memory");
    memset(pa, 0, PGSIZE);
    procs[i].kstack = KSTACK(i);
    kvmmap(pagetable, procs[i].kstack, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

void
proc_freepagetable(pagetable_t pagetable, struct trapframe *trapframe, uint64 sz)
{
  if (pagetable == 0) {
    if (trapframe)
      kfree(trapframe);
    return;
  }

  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  if (trapframe) {
    if (ismapped(pagetable, TRAPFRAME))
      uvmunmap(pagetable, TRAPFRAME, 1, 1);
    else
      kfree(trapframe);
  }
  uvmfree(pagetable, sz);
}

static int
map_image_pages(pagetable_t pagetable, struct prog_info *info)
{
  uint64 text_pages_end = PGROUNDUP(info->text_end);
  uint64 data_start = PGROUNDDOWN(info->data_start);
  uint64 image_end = PGROUNDUP(info->end);

  for (uint64 va = 0; va < text_pages_end; va += PGSIZE) {
    char *mem = kalloc();

    if (mem == 0)
      return -1;
    if (va < info->file_size) {
      uint64 n = info->file_size - va;

      if (n > PGSIZE)
        n = PGSIZE;
      memmove(mem, (void *)(info->start + va), (int)n);
    }
    if (mappages(pagetable, va, PGSIZE, (uint64)mem,
                 PTE_R | PTE_X | PTE_U) != 0) {
      kfree(mem);
      return -1;
    }
  }

  for (uint64 va = data_start; va < image_end; va += PGSIZE) {
    char *mem = kalloc();

    if (mem == 0)
      return -1;
    if (va < info->file_size) {
      uint64 n = info->file_size - va;

      if (n > PGSIZE)
        n = PGSIZE;
      memmove(mem, (void *)(info->start + va), (int)n);
    }
    if (mappages(pagetable, va, PGSIZE, (uint64)mem,
                 PTE_R | PTE_W | PTE_U) != 0) {
      kfree(mem);
      return -1;
    }
  }
  return 0;
}

static int
load_program(const char *name, pagetable_t *pagetable_out,
             struct trapframe **trapframe_out, uint64 *sz_out,
             uint64 *heap_start_out, uint64 *stack_top_out)
{
  struct prog_info info;
  pagetable_t pagetable;
  struct trapframe *trapframe;
  uint64 rollback_sz = 0;
  uint64 image_end, stack_bottom, stack_top, heap_start;

  if (prog_lookup(name, &info) < 0)
    return -1;
  if (info.end == 0 || info.text_end == 0 || info.data_start == 0)
    return -1;

  pagetable = uvmcreate();
  trapframe = kalloc();
  if (pagetable == 0 || trapframe == 0)
    goto bad;
  memset(trapframe, 0, PGSIZE);

  image_end = PGROUNDUP(info.end);
  rollback_sz = image_end;
  if (map_image_pages(pagetable, &info) != 0)
    goto bad;

  stack_bottom = image_end + (uint64)LAB3_GUARD_PAGES * PGSIZE;
  stack_top = stack_bottom + (uint64)USERSTACK * PGSIZE;
  heap_start = PGROUNDUP(stack_top);
  rollback_sz = stack_top;

  for (uint64 va = stack_bottom; va < stack_top; va += PGSIZE) {
    char *mem = kalloc();

    if (mem == 0)
      goto bad;
    if (mappages(pagetable, va, PGSIZE, (uint64)mem,
                 PTE_R | PTE_W | PTE_U) != 0) {
      kfree(mem);
      goto bad;
    }
  }

  if (mappages(pagetable, TRAPFRAME, PGSIZE, (uint64)trapframe,
               PTE_R | PTE_W) != 0)
    goto bad;
  if (mappages(pagetable, TRAMPOLINE, PGSIZE, (uint64)trampoline,
               PTE_R | PTE_X) != 0)
    goto bad;

  trapframe->epc = info.entry;
  trapframe->sp = stack_top - 16;
  trapframe->kernel_satp = MAKE_SATP(kernel_pagetable);
  trapframe->kernel_trap = (uint64)usertrap;
  trapframe->kernel_hartid = r_tp();

  *pagetable_out = pagetable;
  *trapframe_out = trapframe;
  *sz_out = heap_start;
  *heap_start_out = heap_start;
  *stack_top_out = stack_top;
  return 0;

bad:
  if (pagetable)
    proc_freepagetable(pagetable, trapframe, rollback_sz);
  else if (trapframe)
    kfree(trapframe);
  return -1;
}

static void
copy_name(char *dst, const char *src)
{
  int i;

  for (i = 0; i < 15 && src[i]; i++)
    dst[i] = src[i];
  dst[i] = 0;
}

static int
default_tickets(const char *name)
{
  if (strcmp(name, "hi") == 0)
    return 1;
  if (strcmp(name, "spin") == 0)
    return 2;
  if (strcmp(name, "sh") == 0)
    return 4;

  if (LAB4_POLICY_ID == 3 || LAB4_POLICY_ID == 6)
    return 7 + COURSE_SID % 13;
  return 1;
}

static void
set_image(struct proc *p, const char *name)
{
  copy_name(p->name, name);
  p->tickets = default_tickets(name);
  p->tickets_override = 0;
}

static struct proc *
allocproc(void)
{
  for (int i = 0; i < NPROC; i++) {
    struct proc *p = &procs[i];

    acquire(&p->lock);
    if (p->state != UNUSED) {
      release(&p->lock);
      continue;
    }

    p->state = USED;
    p->pid = nextpid++;
    p->parent = -1;
    p->xstatus = 0;
    p->killed = 0;
    p->chan = 0;
    p->pagetable = uvmcreate();
    p->trapframe = kalloc();
    if (p->pagetable == 0 || p->trapframe == 0) {
      if (p->pagetable)
        uvmfree(p->pagetable, 0);
      if (p->trapframe)
        kfree(p->trapframe);
      p->pagetable = 0;
      p->trapframe = 0;
      p->state = UNUSED;
      p->pid = 0;
      release(&p->lock);
      return 0;
    }
    memset(p->trapframe, 0, PGSIZE);

    p->sz = 0;
    p->heap_start = 0;
    p->stack_top = 0;
    p->name[0] = 0;
    p->cpu_ticks = 0;
    p->last_slice_ticks = 0;
    p->tickets = 1;
    p->tickets_override = 0;
    p->pass = stride_virtual;

    p->context.ra = (uint64)forkret;
    p->context.sp = p->kstack + PGSIZE;

    release(&p->lock);
    return p;
  }
  return 0;
}

static void
release_proc_locked(struct proc *p)
{
  proc_freepagetable(p->pagetable, p->trapframe, p->sz);
  p->pagetable = 0;
  p->trapframe = 0;
  p->sz = 0;
  p->heap_start = 0;
  p->stack_top = 0;
  p->chan = 0;
  p->killed = 0;
  p->parent = -1;
  p->pid = 0;
  p->state = UNUSED;
}

void
userinit(void)
{
  struct proc *p = allocproc();
  pagetable_t pagetable;
  struct trapframe *trapframe;
  uint64 sz, heap_start, stack_top;

  if (p == 0)
    panic("userinit: no proc");
  if (load_program("sh", &pagetable, &trapframe, &sz,
                   &heap_start, &stack_top) < 0)
    panic("userinit: cannot load sh");

  p->pid = 1;
  nextpid = 2;
  p->parent = -1;
  p->pagetable = pagetable;
  p->trapframe = trapframe;
  p->sz = sz;
  p->heap_start = heap_start;
  p->stack_top = stack_top;
  trapframe->kernel_sp = p->kstack + PGSIZE;
  set_image(p, "sh");
  initproc = p;

  acquire(&p->lock);
  p->state = RUNNABLE;
  release(&p->lock);

  dump_pagetable(p->pagetable);
}

static uint64
stride_for(int tickets)
{
  uint64 stride;

  if (tickets < 1)
    tickets = 1;
  stride = 1000000ULL / (uint64)tickets;
  return stride ? stride : 1;
}

static int
pick_proc(void)
{
  int best = -1;
  uint64 bestpass = 0;

  for (int step = 1; step <= NPROC; step++) {
    int i = (rr_idx + step + NPROC) % NPROC;
    struct proc *p = &procs[i];

    acquire(&p->lock);
    if (p->state == RUNNABLE &&
        (best < 0 || p->pass < bestpass)) {
      best = i;
      bestpass = p->pass;
    }
    release(&p->lock);
  }

  if (best < 0)
    return -1;

  struct proc *p = &procs[best];
  acquire(&p->lock);
  if (p->state != RUNNABLE) {
    release(&p->lock);
    return -1;
  }

  if (p->pass < stride_virtual)
    p->pass = stride_virtual;
  stride_virtual = p->pass;
  p->pass += stride_for(p->tickets);
  p->last_slice_ticks = 0;
  p->state = RUNNING;
  rr_idx = best;
  return best;
}

void
scheduler(void)
{
  struct cpu *c = mycpu();

  c->proc = 0;
  for (;;) {
    intr_on();
    int idx = pick_proc();

    if (idx < 0) {
      intr_on();
      asm volatile("wfi");
      continue;
    }

    struct proc *p = &procs[idx];
    c->proc = p;
    swtch(&c->context, &p->context);
    c->proc = 0;
    release(&p->lock);
  }
}

void
sched(void)
{
  struct proc *p = myproc();
  struct cpu *c = mycpu();

  if (!holding(&p->lock))
    panic("sched p->lock");
  if (c->noff != 1)
    panic("sched locks");
  if (p->state == RUNNING)
    panic("sched running");
  if (intr_get())
    panic("sched interruptible");

  int intena = c->intena;
  swtch(&p->context, &c->context);
  c->intena = intena;
}

void
yield(void)
{
  struct proc *p = myproc();

  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  if (lk == &p->lock)
    panic("sleep: p->lock");

  acquire(&p->lock);
  release(lk);

  p->chan = chan;
  p->state = SLEEPING;
  sched();

  p->chan = 0;
  release(&p->lock);
  acquire(lk);
}

void
wakeup(void *chan)
{
  for (int i = 0; i < NPROC; i++) {
    struct proc *p = &procs[i];

    if (p == myproc())
      continue;
    acquire(&p->lock);
    if (p->state == SLEEPING && p->chan == chan) {
      p->state = RUNNABLE;
      if (p->pass < stride_virtual)
        p->pass = stride_virtual;
    }
    release(&p->lock);
  }
}

static void
forkret(void)
{
  struct proc *p = myproc();

  release(&p->lock);
  usertrapret();
}

uint64
sys_fork(void)
{
  struct proc *p = myproc();
  struct proc *c = allocproc();

  if (c == 0)
    return (uint64)-1;

  acquire(&wait_lock);
  c->parent = (int)(p - procs);
  release(&wait_lock);

  c->sz = p->sz;
  c->heap_start = p->heap_start;
  c->stack_top = p->stack_top;
  copy_name(c->name, p->name);
  c->tickets = p->tickets;
  c->tickets_override = p->tickets_override;
  c->pass = p->pass < stride_virtual ? stride_virtual : p->pass;

  if (uvmcopy(p->pagetable, c->pagetable, p->sz) < 0)
    goto bad;

  *c->trapframe = *p->trapframe;
  c->trapframe->a0 = 0;
  c->trapframe->kernel_satp = MAKE_SATP(kernel_pagetable);
  c->trapframe->kernel_sp = c->kstack + PGSIZE;
  c->trapframe->kernel_trap = (uint64)usertrap;
  c->trapframe->kernel_hartid = r_tp();

  if (mappages(c->pagetable, TRAPFRAME, PGSIZE,
               (uint64)c->trapframe, PTE_R | PTE_W) != 0)
    goto bad;
  if (mappages(c->pagetable, TRAMPOLINE, PGSIZE,
               (uint64)trampoline, PTE_R | PTE_X) != 0)
    goto bad;

  acquire(&c->lock);
  c->state = RUNNABLE;
  int pid = c->pid;
  release(&c->lock);
  return (uint64)pid;

bad:
  acquire(&c->lock);
  release_proc_locked(c);
  release(&c->lock);
  return (uint64)-1;
}

uint64
sys_wait(void)
{
  struct proc *p = myproc();
  int me = (int)(p - procs);
  uint64 status_addr;

  argaddr(0, &status_addr);

  acquire(&wait_lock);
  for (;;) {
    int havekids = 0;

    for (int i = 0; i < NPROC; i++) {
      struct proc *c = &procs[i];

      acquire(&c->lock);
      if (c->parent == me) {
        havekids = 1;
        if (c->state == ZOMBIE) {
          int pid = c->pid;
          int status = c->xstatus;

          release_proc_locked(c);
          release(&c->lock);
          release(&wait_lock);
          if (status_addr)
            copyout(p->pagetable, p->sz, status_addr,
                    (char *)&status, sizeof(status));
          return (uint64)pid;
        }
      }
      release(&c->lock);
    }

    if (!havekids || p->killed) {
      release(&wait_lock);
      return (uint64)-1;
    }
    sleep(p, &wait_lock);
  }
}

void
proc_exit(int status)
{
  struct proc *p = myproc();
  int me = (int)(p - procs);

  if (p->parent < 0) {
    printf("proc %d exit with no parent\n", p->pid);
    halt();
  }

  acquire(&wait_lock);

  for (int i = 0; i < NPROC; i++) {
    struct proc *q = &procs[i];

    if (q == p)
      continue;
    acquire(&q->lock);
    if (q->parent == me && q->state != UNUSED)
      q->parent = (int)(initproc - procs);
    release(&q->lock);
  }

  acquire(&p->lock);
  p->xstatus = status;
  p->state = ZOMBIE;
  wakeup(&procs[p->parent]);
  release(&wait_lock);
  sched();
  panic("proc_exit returned");
}

uint64
sys_exit(void)
{
  int status;

  argint(0, &status);
  proc_exit(status);
}

uint64
sys_exec(void)
{
  char name[64];
  struct proc *p = myproc();
  pagetable_t newpagetable;
  struct trapframe *newtrapframe;
  uint64 newsz, newheap, newstack;
  int i = 0, j = 0;

  if (argstr(0, name, sizeof(name)) < 0)
    return (uint64)-1;

  while (name[i] == ' ' || name[i] == '\t')
    i++;
  while (name[i] && name[i] != ' ' && name[i] != '\t' &&
         name[i] != '\n' && name[i] != '\r')
    name[j++] = name[i++];
  name[j] = 0;

  if (load_program(name, &newpagetable, &newtrapframe, &newsz,
                   &newheap, &newstack) < 0)
    return (uint64)-1;

  proc_freepagetable(p->pagetable, p->trapframe, p->sz);
  p->pagetable = newpagetable;
  p->trapframe = newtrapframe;
  p->sz = newsz;
  p->heap_start = newheap;
  p->stack_top = newstack;
  p->trapframe->kernel_sp = p->kstack + PGSIZE;
  p->trapframe->kernel_satp = MAKE_SATP(kernel_pagetable);
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();
  set_image(p, name);
  p->pass = stride_virtual;
  p->cpu_ticks = 0;
  p->last_slice_ticks = 0;
  return 0;
}

uint64
sys_sbrk(void)
{
  struct proc *p = myproc();
  int n, which;
  uint64 oldsz = p->sz;

  argint(0, &n);
  argint(1, &which);
  (void)which;

  if (n == 0)
    return oldsz;

  if (n < 0) {
    uint64 newsz = oldsz + (uint64)n;
    uint64 oldpg, newpg;

    if (newsz < p->heap_start || newsz > oldsz)
      return (uint64)-1;
    oldpg = PGROUNDUP(oldsz);
    newpg = PGROUNDUP(newsz);
    if (oldpg > newpg)
      uvmunmap(p->pagetable, newpg, (oldpg - newpg) / PGSIZE, 1);
    p->sz = newsz;
    return oldsz;
  }

  uint64 newsz = oldsz + (uint64)n;
  if (newsz < oldsz || newsz > (uint64)TRAPFRAME)
    return (uint64)-1;

  uint64 start = PGROUNDUP(oldsz);
  uint64 end = PGROUNDUP(newsz);
  uint64 mapped = 0;

  for (uint64 va = start; va < end; va += PGSIZE) {
    char *mem = kalloc();

    if (mem == 0)
      goto rollback;
    if (mappages(p->pagetable, va, PGSIZE, (uint64)mem,
                 PTE_R | PTE_W | PTE_U) != 0) {
      kfree(mem);
      goto rollback;
    }
    mapped++;
  }

  p->sz = newsz;
  return oldsz;

rollback:
  if (mapped)
    uvmunmap(p->pagetable, start, mapped, 1);
  return (uint64)-1;
}
