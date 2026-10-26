// Physical memory layout for QEMU virt.
//
// 00001000 -- boot ROM
// 02000000 -- CLINT
// 0C000000 -- PLIC
// 10000000 -- UART0
// 10001000 -- virtio disk
// 80000000 -- kernel load address and start of RAM

#define UART0     0x10000000L
#define UART0_IRQ 10

#define VIRTIO0     0x10001000
#define VIRTIO0_IRQ 1

// Core-local interrupt controller. This kernel uses rdtime/stimecmp instead
// of programming CLINT through MMIO, so CLINT is not mapped.
#define CLINT_BASE  0x02000000L
#define CLINT(hart) (CLINT_BASE + (hart) * 4)

#define PLIC                 0x0c000000L
#define PLIC_PRIORITY        (PLIC + 0x0)
#define PLIC_PENDING         (PLIC + 0x1000)
#define PLIC_SENABLE(hart)   (PLIC + 0x2080 + (hart) * 0x100)
#define PLIC_SPRIORITY(hart) (PLIC + 0x201000 + (hart) * 0x2000)
#define PLIC_SCLAIM(hart)    (PLIC + 0x201004 + (hart) * 0x2000)

#define KERNBASE 0x80000000L
#define PHYSTOP  (KERNBASE + 128 * 1024 * 1024)

// The trampoline is mapped at the same high virtual address in every user
// page table and in the kernel page table.
#define TRAMPOLINE (MAXVA - PGSIZE)

// Kernel stacks sit below the trampoline, each with a guard page.
#define KSTACK(p) (TRAMPOLINE - ((p) + 1) * 2 * PGSIZE)

// User address space from low to high:
//   text/rodata (R+X)
//   data/bss (R+W)
//   LAB3_GUARD_PAGES unmapped guard pages
//   user stack (R+W, grows down)
//   sbrk heap (R+W, grows up)
//   TRAPFRAME (one per process, mapped only in its user page table)
//   TRAMPOLINE (shared code page)
#define TRAPFRAME (TRAMPOLINE - PGSIZE)
