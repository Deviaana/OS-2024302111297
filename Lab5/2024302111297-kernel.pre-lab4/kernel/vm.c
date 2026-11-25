#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "vm.h"
#include "proc.h"
#include "defs.h"

pagetable_t kernel_pagetable;

extern char trampoline[];

static volatile int cow_locked;

static uint64
cow_lock(void)
{
  uint64 flags = intr_get();

  intr_off();
  while (__sync_lock_test_and_set(&cow_locked, 1) != 0)
    ;
  return flags;
}

static void
cow_unlock(uint64 flags)
{
  __sync_lock_release(&cow_locked);
  if (flags)
    intr_on();
}

static int table_empty(pagetable_t);

pagetable_t
uvmcreate(void)
{
  pagetable_t pagetable = (pagetable_t)kalloc();

  if (pagetable)
    memset(pagetable, 0, PGSIZE);
  return pagetable;
}

void
kvmmap(pagetable_t pagetable, uint64 va, uint64 pa, uint64 size, int perm)
{
  if (mappages(pagetable, va, size, pa, perm) != 0)
    panic("kvmmap");
}

static pagetable_t
kvmmake(void)
{
  pagetable_t pagetable = uvmcreate();

  if (pagetable == 0)
    panic("kvmmake: no memory");

  /* Devices the kernel touches directly. CLINT is intentionally absent:
   * this kernel uses the rdtime/stimecmp CSRs and never MMIOs CLINT. */
  kvmmap(pagetable, UART0, UART0, PGSIZE, PTE_R | PTE_W);
  kvmmap(pagetable, PLIC, PLIC, 0x4000000, PTE_R | PTE_W);

  /* Identity-map the complete kernel image and all managed RAM. Paging is
   * enabled before the first process exists, so page tables and user pages
   * allocated outside the image must remain directly addressable as well. */
  kvmmap(pagetable, KERNBASE, KERNBASE, PHYSTOP - KERNBASE,
         PTE_R | PTE_W | PTE_X);

  /* The trampoline must have the same virtual address in every address
   * space. It contains executable code but is never writable by user mode. */
  kvmmap(pagetable, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  proc_mapstacks(pagetable);
  return pagetable;
}

void
kvminit(void)
{
  kernel_pagetable = kvmmake();
}

void
kvminithart(void)
{
  sfence_vma();
  w_satp(MAKE_SATP(kernel_pagetable));
  sfence_vma();
}

pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if (va >= MAXVA)
    return 0;

  for (int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];

    if (*pte & PTE_V) {
      if ((*pte & (PTE_R | PTE_W | PTE_X)) != 0)
        return 0;
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      if (!alloc)
        return 0;
      pagetable = uvmcreate();
      if (pagetable == 0)
        return 0;
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }

  return &pagetable[PX(0, va)];
}

uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if (va >= MAXVA)
    return 0;
  pte = walk(pagetable, va, 0);
  if (pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0)
    return 0;

  pa = PTE2PA(*pte);
  return pa;
}

static void
free_empty_tables(pagetable_t pagetable, int level, uint64 va, uint64 end)
{
  if (level == 0 || va >= end)
    return;

  uint64 first = PX(level, va);
  uint64 last = PX(level, end - 1);

  for (uint64 i = first; i <= last; i++) {
    pte_t *pte = &pagetable[i];

    if ((*pte & PTE_V) == 0 || (*pte & (PTE_R | PTE_W | PTE_X)) != 0)
      continue;

    pagetable_t child = (pagetable_t)PTE2PA(*pte);
    uint64 child_base = i << PXSHIFT(level);
    uint64 child_end = child_base + (1UL << PXSHIFT(level));
    uint64 lo = va > child_base ? va : child_base;
    uint64 hi = end < child_end ? end : child_end;

    free_empty_tables(child, level - 1, lo, hi);
    if (table_empty(child)) {
      kfree(child);
      *pte = 0;
    }
  }
}

static int
table_empty(pagetable_t pagetable)
{
  for (int i = 0; i < 512; i++)
    if (pagetable[i] & PTE_V)
      return 0;
  return 1;
}

int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  if ((va % PGSIZE) != 0 || (pa % PGSIZE) != 0 ||
      (size % PGSIZE) != 0 || size == 0)
    return -1;
  if (va >= MAXVA || size > MAXVA - va)
    return -1;

  a = va;
  last = va + size - PGSIZE;
  for (;;) {
    pte = walk(pagetable, a, 1);
    if (pte == 0 || (*pte & PTE_V))
      goto rollback;

    *pte = PA2PTE(pa) | perm | PTE_V;
    if (a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  sfence_vma();
  return 0;

rollback:
  /*
   * The physical pages described by pa belong to the caller and are not
   * released here. Only PTEs installed by this call and empty intermediate
   * page-table pages created while walking the range are rolled back.
   */
  if (a > va)
    uvmunmap(pagetable, va, (a - va) / PGSIZE, 0);
  free_empty_tables(pagetable, 2, va, PGROUNDUP(a + PGSIZE));
  sfence_vma();
  return -1;
}

void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  if ((va % PGSIZE) != 0)
    panic("uvmunmap: unaligned va");

  for (uint64 i = 0; i < npages; i++) {
    uint64 a = va + i * PGSIZE;
    pte_t *pte;

    if (a >= MAXVA)
      break;
    pte = walk(pagetable, a, 0);
    if (pte == 0 || (*pte & PTE_V) == 0)
      continue;

    uint64 pa = PTE2PA(*pte);
    *pte = 0;
    if (do_free)
      kfree((void *)pa);
  }
  sfence_vma();
}

static void
freewalk(pagetable_t pagetable)
{
  for (int i = 0; i < 512; i++) {
    pte_t pte = pagetable[i];

    if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0) {
      pagetable_t child = (pagetable_t)PTE2PA(pte);

      freewalk(child);
      pagetable[i] = 0;
    } else if (pte & PTE_V) {
      panic("freewalk: leaf page still mapped");
    }
  }
  kfree(pagetable);
}

void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if (sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz) / PGSIZE, 1);
  freewalk(pagetable);
}

int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  for (uint64 va = 0; va < sz; va += PGSIZE) {
    pte_t *pte = walk(old, va, 0);

    /* Guard-page holes and other unmapped gaps are legal in this layout. */
    if (pte == 0 || (*pte & PTE_V) == 0)
      continue;
    if ((*pte & PTE_U) == 0)
      continue;

    uint64 pa = PTE2PA(*pte);
    uint64 flags = *pte & PTE_FLAGS(*pte);

    if (flags & PTE_W) {
      flags &= ~(PTE_W | PTE_COW | PTE_RSW1);
      flags |= PTE_COW;
#if LAB5_REFCNT_SCHEME == 0
      if (kref_get(pa) >= 2)
        flags |= PTE_RSW1;
#endif
      *pte = PA2PTE(pa) | flags;
    }

    if (mappages(new, va, PGSIZE, pa, (int)flags) != 0)
      goto bad;
    kref_inc(pa);
  }
  sfence_vma();
  return 0;

bad:
  uvmunmap(new, 0, PGROUNDUP(sz) / PGSIZE, 1);
  sfence_vma();
  return -1;
}

int
cow_alloc(pagetable_t pagetable, uint64 va)
{
  uint64 flags;
  pte_t *pte;
  uint64 pteval;
  uint64 old_pa, pte_flags;

  if (va >= MAXVA)
    return -1;

  flags = cow_lock();
  pte = walk(pagetable, PGROUNDDOWN(va), 0);
  if (pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0) {
    cow_unlock(flags);
    return -1;
  }

  pteval = *pte;
  pte_flags = pteval & PTE_FLAGS(pteval);
  if (pte_flags & PTE_W) {
    cow_unlock(flags);
    return 0;
  }
  if ((pte_flags & PTE_COW) == 0) {
    cow_unlock(flags);
    return -1;
  }

  old_pa = PTE2PA(pteval);
  if (kref_get(old_pa) == 1) {
    pte_flags &= ~(PTE_COW | PTE_RSW1);
    pte_flags |= PTE_W;
    *pte = PA2PTE(old_pa) | pte_flags;
    sfence_vma();
    cow_unlock(flags);
    return 0;
  }

  struct proc *p = myproc();

  if (p && p->pagetable == pagetable) {
    p->cow_copies++;
    if (p->cow_copies == LAB5_COW_BUDGET)
      printf("[cow] pid=%d reached LAB5_COW_BUDGET=%d, "
             "downgrade: continue copying\n",
             p->pid, LAB5_COW_BUDGET);
  }

  char *new_pa = kalloc();

  if (new_pa == 0) {
    cow_unlock(flags);
    return -1;
  }

  memmove(new_pa, (void *)old_pa, PGSIZE);
  pte_flags &= ~(PTE_COW | PTE_RSW1);
  pte_flags |= PTE_W;
  *pte = PA2PTE(new_pa) | pte_flags;
  kcow_note_copy();
  kfree((void *)old_pa);
  sfence_vma();
  cow_unlock(flags);
  return 0;
}

void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte = walk(pagetable, va, 0);

  if (pte == 0 || (*pte & PTE_V) == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

int
copyout(pagetable_t pagetable, uint64 sz, uint64 dstva, char *src, uint64 len)
{
  while (len > 0) {
    uint64 va0 = PGROUNDDOWN(dstva);
    uint64 n;
    pte_t *pte;

    if (va0 >= MAXVA || va0 >= sz)
      return -1;
    pte = walk(pagetable, va0, 0);
    if (pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0)
      return -1;
    if ((*pte & PTE_W) == 0) {
      if ((*pte & PTE_COW) == 0 || cow_alloc(pagetable, va0) < 0)
        return -1;
      pte = walk(pagetable, va0, 0);
      if (pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_W) == 0)
        return -1;
    }

    n = PGSIZE - (dstva - va0);
    if (n > len)
      n = len;
    memmove((void *)(PTE2PA(*pte) + (dstva - va0)), src, n);
    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

int
copyin(pagetable_t pagetable, uint64 sz, char *dst, uint64 srcva, uint64 len)
{
  while (len > 0) {
    uint64 va0 = PGROUNDDOWN(srcva);
    uint64 n;
    pte_t *pte;

    if (va0 >= MAXVA || va0 >= sz)
      return -1;
    pte = walk(pagetable, va0, 0);
    if (pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0 ||
        (*pte & PTE_R) == 0)
      return -1;

    n = PGSIZE - (srcva - va0);
    if (n > len)
      n = len;
    memmove(dst, (void *)(PTE2PA(*pte) + (srcva - va0)), n);
    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

int
copyinstr(pagetable_t pagetable, uint64 sz, char *dst, uint64 srcva, uint64 max)
{
  uint64 copied = 0;

  while (copied < max) {
    char c;

    if (copyin(pagetable, sz, &c, srcva + copied, 1) != 0)
      return -1;
    dst[copied] = c;
    if (c == 0)
      return 0;
    copied++;
  }
  return -1;
}

int
ismapped(pagetable_t pagetable, uint64 va)
{
  pte_t *pte = walk(pagetable, va, 0);

  return pte != 0 && (*pte & PTE_V) != 0;
}

static void
dump_leaf(uint64 va, uint64 pa, uint64 pte)
{
  printf("LEAF va=0x%p pa=0x%p perm=%c%c%c%c\n", va, pa,
         (pte & PTE_R) ? 'r' : '-',
         (pte & PTE_W) ? 'w' : '-',
         (pte & PTE_X) ? 'x' : '-',
         (pte & PTE_U) ? 'u' : '-');
}

static void
dump_level(pagetable_t pagetable, int level, uint64 base)
{
  for (int i = 0; i < 512; i++) {
    uint64 pte = pagetable[i];
    uint64 va;

    if ((pte & PTE_V) == 0)
      continue;
    va = base | ((uint64)i << PXSHIFT(level));

    if (level == 0) {
      dump_leaf(va, PTE2PA(pte), pte);
    } else if ((pte & (PTE_R | PTE_W | PTE_X)) == 0) {
      pagetable_t child = (pagetable_t)PTE2PA(pte);

      printf("L%d va=0x%p pa=0x%p perm=----\n", level, va, PTE2PA(pte));
      dump_level(child, level - 1, va);
    } else {
      dump_leaf(va, PTE2PA(pte), pte);
    }
  }
}

void
dump_pagetable(pagetable_t pagetable)
{
  printf("DUMP-PAGETABLE begin\n");
  if (pagetable)
    dump_level(pagetable, 2, 0);
  printf("DUMP-PAGETABLE end\n");
}
