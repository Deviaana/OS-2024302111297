#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "proc.h"
#include "syscall.h"
#include "defs.h"

struct proc procs[NPROC];

static struct proc *current;
static int nextpid = 2;

extern char trampoline[];
extern char _uprog_table[];
extern uint64 _uprog_entry[];

struct uprog {
  uint64 start;       /* embedded flat image address */
  uint64 end;         /* virtual image end, including bss */
  uint64 text_end;    /* end of R+X text/rodata */
  uint64 data_start;  /* page-aligned start of R+W data */
  uint64 file_size;   /* bytes actually present in the flat binary */
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

struct proc *
myproc(void)
{
  return current;
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

  /*
   * Record the image range before mapping starts.  If kalloc fails halfway
   * through map_image_pages(), the cleanup path must still unmap every leaf
   * that was installed; leaving rollback_sz at zero would feed freewalk() a
   * table with live leaf PTEs and panic.
   */
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
  trapframe->kernel_hartid = 0;

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

static struct proc *
proc_alloc(void)
{
  for (int i = 1; i < NPROC; i++) {
    struct proc *p = &procs[i];

    if (p->state != UNUSED)
      continue;

    p->pagetable = uvmcreate();
    p->trapframe = kalloc();
    if (p->pagetable == 0 || p->trapframe == 0) {
      if (p->pagetable)
        uvmfree(p->pagetable, 0);
      if (p->trapframe)
        kfree(p->trapframe);
      p->pagetable = 0;
      p->trapframe = 0;
      return 0;
    }
    memset(p->trapframe, 0, PGSIZE);
    p->state = RUNNABLE;
    p->pid = nextpid++;
    p->parent = -1;
    p->xstatus = 0;
    p->sz = 0;
    p->heap_start = 0;
    p->stack_top = 0;
    return p;
  }
  return 0;
}

void
userinit(void)
{
  struct proc *p = &procs[0];
  pagetable_t pagetable;
  struct trapframe *trapframe;
  uint64 sz, heap_start, stack_top;

  p->state = RUNNING;
  p->pid = 1;
  p->parent = -1;
  p->xstatus = 0;

  if (load_program("sh", &pagetable, &trapframe, &sz,
                   &heap_start, &stack_top) < 0)
    panic("userinit: cannot load sh");

  p->pagetable = pagetable;
  p->trapframe = trapframe;
  p->sz = sz;
  p->heap_start = heap_start;
  p->stack_top = stack_top;
  trapframe->kernel_sp = p->kstack + PGSIZE;
  current = p;

  dump_pagetable(p->pagetable);
  usertrapret();
}

static void
release_proc(struct proc *p)
{
  proc_freepagetable(p->pagetable, p->trapframe, p->sz);
  p->pagetable = 0;
  p->trapframe = 0;
  p->sz = 0;
  p->heap_start = 0;
  p->stack_top = 0;
  p->state = UNUSED;
  p->pid = 0;
  p->parent = -1;
}

uint64
sys_fork(void)
{
  struct proc *p = myproc();
  struct proc *c = proc_alloc();

  if (c == 0)
    return (uint64)-1;

  c->parent = (int)(p - procs);
  c->sz = p->sz;
  c->heap_start = p->heap_start;
  c->stack_top = p->stack_top;

  if (uvmcopy(p->pagetable, c->pagetable, p->sz) < 0)
    goto bad;

  *c->trapframe = *p->trapframe;
  c->trapframe->a0 = 0;
  c->trapframe->kernel_satp = MAKE_SATP(kernel_pagetable);
  c->trapframe->kernel_sp = c->kstack + PGSIZE;
  c->trapframe->kernel_trap = (uint64)usertrap;
  c->trapframe->kernel_hartid = 0;

  if (mappages(c->pagetable, TRAPFRAME, PGSIZE,
               (uint64)c->trapframe, PTE_R | PTE_W) != 0)
    goto bad;
  if (mappages(c->pagetable, TRAMPOLINE, PGSIZE,
               (uint64)trampoline, PTE_R | PTE_X) != 0)
    goto bad;

  return (uint64)c->pid;

bad:
  release_proc(c);
  return (uint64)-1;
}

uint64
sys_wait(void)
{
  struct proc *p = myproc();
  int me = (int)(p - procs);
  uint64 status_addr;

  argaddr(0, &status_addr);

  for (int i = 0; i < NPROC; i++) {
    struct proc *c = &procs[i];

    if (c->parent == me && c->state == ZOMBIE) {
      int pid = c->pid;

      if (status_addr)
        copyout(p->pagetable, p->sz, status_addr,
                (char *)&c->xstatus, sizeof(c->xstatus));
      release_proc(c);
      return (uint64)pid;
    }
  }

  for (int i = 0; i < NPROC; i++) {
    struct proc *c = &procs[i];

    if (c->parent == me && c->state == RUNNABLE) {
      p->state = BLOCKED;
      c->state = RUNNING;
      current = c;
      return 0;
    }
  }

  return (uint64)-1;
}

void
proc_exit(int status)
{
  struct proc *p = myproc();

  p->state = ZOMBIE;
  p->xstatus = status;

  if (p->parent < 0) {
    printf("proc %d exit with no parent\n", p->pid);
    halt();
  }

  struct proc *parent = &procs[p->parent];

  if (parent->state != BLOCKED) {
    printf("proc %d exit: parent %d is not waiting\n", p->pid, p->parent);
    halt();
  }

  if (parent->trapframe->a0)
    copyout(parent->pagetable, parent->sz, parent->trapframe->a0,
            (char *)&status, sizeof(status));
  parent->trapframe->a0 = (uint64)p->pid;
  parent->state = RUNNING;

  release_proc(p);
  current = parent;
}

uint64
sys_exit(void)
{
  int status;

  argint(0, &status);
  proc_exit(status);
  return 0;
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
  p->trapframe->kernel_hartid = 0;
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
