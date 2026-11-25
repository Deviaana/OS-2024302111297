#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "course_sid.h"
#include "defs.h"

/*
 * Physical page allocator.
 *
 * A bitmap is used instead of an address-ordered free list: kfree is O(1),
 * which matters for the memcap test that releases tens of thousands of pages.
 * One bit describes one 4 KiB page; 128 MiB of RAM needs only 4 KiB of bitmap.
 */

#define MAX_MANAGED_PAGES ((PHYSTOP - KERNBASE) / PGSIZE)
#define BITMAP_BYTES      (MAX_MANAGED_PAGES / 8)
#define TOTAL_PAGES       ((PHYSTOP - KERNBASE) / PGSIZE)

extern char kernel_end[];

static uchar used[BITMAP_BYTES];
static uint64 managed_start;
static uint64 managed_pages;
static uint64 alloc_limit;
static uint64 free_pages;

/*
 * refcnt[] is indexed by physical page number relative to KERNBASE. It is
 * protected by the same lock as the bitmap so that releasing a page and
 * updating its reference count are one atomic operation.
 */
static uint16 refcnt[TOTAL_PAGES];
static uint64 cow_copies;
static volatile int kmem_locked;

static uint64
kmem_lock(void)
{
  uint64 flags = intr_get();

  intr_off();
  while (__sync_lock_test_and_set(&kmem_locked, 1) != 0)
    ;
  return flags;
}

static void
kmem_unlock(uint64 flags)
{
  __sync_lock_release(&kmem_locked);
  if (flags)
    intr_on();
}

static uint64
ref_index(uint64 pa)
{
  if (pa < KERNBASE || pa >= PHYSTOP || (pa % PGSIZE) != 0)
    panic("kalloc: invalid physical page");
  return (pa - KERNBASE) / PGSIZE;
}

static int
bit_test(uint64 i)
{
  return (used[i / 8] >> (i % 8)) & 1;
}

static void
bit_set(uint64 i, int value)
{
  uchar mask = (uchar)(1U << (i % 8));

  if (value)
    used[i / 8] |= mask;
  else
    used[i / 8] &= (uchar)~mask;
}

static uint64
page_index(void *pa)
{
  return ((uint64)pa - managed_start) / PGSIZE;
}

static void *
page_address(uint64 index)
{
  return (void *)(managed_start + index * PGSIZE);
}

void
kinit(void)
{
  /*
   * kernel_end is supplied by kernel.ld.  Never hand the linker's last
   * partially used page to the allocator.
   */
  managed_start = PGROUNDUP((uint64)kernel_end);
  managed_pages = (PHYSTOP - managed_start) / PGSIZE;

  if (managed_pages == 0)
    panic("kinit: no managed memory");

  /*
   * The course parameter reserves LAB3_MEMCAP_DELTA pages from the total
   * page count.  Those pages remain permanently marked used.
   */
  alloc_limit = managed_pages;
  if (alloc_limit > (uint64)LAB3_MEMCAP_DELTA)
    alloc_limit -= (uint64)LAB3_MEMCAP_DELTA;
  else
    alloc_limit = 0;

  for (uint64 i = 0; i < BITMAP_BYTES; i++)
    used[i] = 0xff;
  for (uint64 i = 0; i < TOTAL_PAGES; i++)
    refcnt[i] = 0;

  free_pages = 0;
  for (uint64 i = 0; i < alloc_limit; i++) {
    bit_set(i, 0);
    refcnt[ref_index((uint64)page_address(i))] = 1;
    free_pages++;
  }
}

static int
take_page(uint64 index)
{
  if (index >= alloc_limit || bit_test(index))
    return 0;

  bit_set(index, 1);
  free_pages--;
  memset(page_address(index), 0, PGSIZE);
  return 1;
}

void *
kalloc(void)
{
  uint64 index = 0;
  uint64 flags;
  int found = 0;

  if (alloc_limit == 0)
    return 0;

  flags = kmem_lock();

#if LAB3_ALLOC_ORDER == 0
  /* Ascending: lowest physical address first. */
  for (uint64 i = 0; i < alloc_limit; i++) {
    if (take_page(i)) {
      index = i;
      found = 1;
      break;
    }
  }
#elif LAB3_ALLOC_ORDER == 2
  /*
   * Middle-out: start at the midpoint, then alternate above/below it.
   * This preserves a deterministic order without an extra free-list.
   */
  uint64 mid = alloc_limit / 2;
  for (uint64 d = 0; d < alloc_limit && !found; d++) {
    uint64 above = mid + d;

    if (above < alloc_limit && take_page(above)) {
      index = above;
      found = 1;
      break;
    }
    if (d != 0 && mid >= d) {
      uint64 below = mid - d;

      if (take_page(below)) {
        index = below;
        found = 1;
        break;
      }
    }
  }
#else
  /* Descending (the value selected by this student ID). */
  for (uint64 i = alloc_limit; i > 0; i--) {
    if (take_page(i - 1)) {
      index = i - 1;
      found = 1;
      break;
    }
  }
#endif

  if (!found) {
    kmem_unlock(flags);
    return 0;
  }

  refcnt[ref_index((uint64)page_address(index))] = 1;
  kmem_unlock(flags);
  return page_address(index);
}

void
kfree(void *pa)
{
  uint64 index;
  uint64 flags;

  if (pa == 0 || ((uint64)pa % PGSIZE) != 0)
    panic("kfree: unaligned pointer");
  if ((uint64)pa < managed_start ||
      (uint64)pa >= managed_start + managed_pages * PGSIZE)
    panic("kfree: pointer outside managed memory");

  flags = kmem_lock();
  index = page_index(pa);
  if (index >= alloc_limit) {
    kmem_unlock(flags);
    panic("kfree: page belongs to reserved quota");
  }

  uint64 ri = ref_index((uint64)pa);
  if (refcnt[ri] < 1) {
    kmem_unlock(flags);
    panic("kfree: refcnt underflow");
  }
  if (refcnt[ri] == 1 && !bit_test(index)) {
    kmem_unlock(flags);
    panic("kfree: double free");
  }

  refcnt[ri]--;
  if (refcnt[ri] > 0) {
    kmem_unlock(flags);
    return;
  }

  /* Poison only after the last mapping has gone away. */
  memset(pa, 0x5a, PGSIZE);
  bit_set(index, 0);
  free_pages++;
  kmem_unlock(flags);
}

uint64
kfreepages(void)
{
  return free_pages;
}

void
kref_inc(uint64 pa)
{
  uint64 flags = kmem_lock();
  uint64 ri = ref_index(pa);

  if (refcnt[ri] < 1)
    panic("kref_inc: page is not allocated");
  if (refcnt[ri] == 0xffff)
    panic("kref_inc: reference count overflow");
  refcnt[ri]++;
  kmem_unlock(flags);
}

uint64
kref_get(uint64 pa)
{
  uint64 flags = kmem_lock();
  uint64 count = refcnt[ref_index(pa)];

  kmem_unlock(flags);
  return count;
}

uint64
kshared_count(void)
{
  uint64 flags = kmem_lock();
  uint64 count = 0;

  for (uint64 i = 0; i < alloc_limit; i++)
    if (refcnt[ref_index((uint64)page_address(i))] > 1)
      count++;
  kmem_unlock(flags);
  return count;
}

uint64
kcow_copy_count(void)
{
  uint64 flags = kmem_lock();
  uint64 count = cow_copies;

  kmem_unlock(flags);
  return count;
}

void
kcow_note_copy(void)
{
  uint64 flags = kmem_lock();

  cow_copies++;
  kmem_unlock(flags);
}

void
kref_selftest(void)
{
  uint64 free0 = kfreepages();
  char *p = kalloc();

  if (p == 0)
    panic("kref_selftest: kalloc failed");
  p[0] = 0x37;
  if (kref_get((uint64)p) != 1)
    panic("kref_selftest: initial refcnt");

  kref_inc((uint64)p);
  kref_inc((uint64)p);
  if (kref_get((uint64)p) != 3 || kshared_count() != 1)
    panic("kref_selftest: shared accounting");

  kfree(p);
  kfree(p);
  if (kref_get((uint64)p) != 1 || p[0] != 0x37)
    panic("kref_selftest: early release poisoned page");

  kfree(p);
  if (kfreepages() != free0 || kshared_count() != 0)
    panic("kref_selftest: final accounting");
  printf("LAB5 kref selftest: PASS\n");
}
