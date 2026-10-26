/* Global kernel declarations. */
#ifndef DEFS_H
#define DEFS_H

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "course_sid.h"

struct proc;
struct trapframe;

/* printf.c */
void printf(const char *fmt, ...);

/* console.c */
void consoleinit(void);
void consputc(int c);
void console_banner(void);
void uartintr(void);
int consoleread(char *dst, int n);

/* trap.c */
void usertrap(void);
void usertrapret(void) __attribute__((noreturn));
void trapinithart(void);
void kerneltrap(void);
uint64 uptime(void);
void panic(const char *msg) __attribute__((noreturn));
void halt(void) __attribute__((noreturn));

/* plic.c */
void plicinit(void);
void plicinithart(void);
int plic_claim(void);
void plic_complete(int irq);

/* kalloc.c */
void kinit(void);
void *kalloc(void);
void kfree(void *pa);
uint64 kfreepages(void);

/* vm.c */
extern pagetable_t kernel_pagetable;
void kvminit(void);
void kvminithart(void);
void kvmmap(pagetable_t pagetable, uint64 va, uint64 pa, uint64 size, int perm);
pte_t *walk(pagetable_t pagetable, uint64 va, int alloc);
uint64 walkaddr(pagetable_t pagetable, uint64 va);
int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm);
pagetable_t uvmcreate(void);
void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free);
void uvmfree(pagetable_t pagetable, uint64 sz);
int uvmcopy(pagetable_t old, pagetable_t new, uint64 sz);
void uvmclear(pagetable_t pagetable, uint64 va);
int copyin(pagetable_t pagetable, uint64 sz, char *dst, uint64 srcva, uint64 len);
int copyout(pagetable_t pagetable, uint64 sz, uint64 dstva, char *src, uint64 len);
int copyinstr(pagetable_t pagetable, uint64 sz, char *dst, uint64 srcva, uint64 max);
int ismapped(pagetable_t pagetable, uint64 va);
void dump_pagetable(pagetable_t pagetable);

/* proc.c */
void userinit(void);
struct proc *myproc(void);
void proc_mapstacks(pagetable_t pagetable);
void proc_freepagetable(pagetable_t pagetable, struct trapframe *trapframe, uint64 sz);
void proc_exit(int status);

/* syscall.c */
void syscall(void);
void argint(int n, int *ip);
void argaddr(int n, uint64 *ip);
int argstr(int n, char *buf, int max);

/* string.c */
void *memmove(void *dst, const void *src, int n);
void *memset(void *dst, int c, uint n);
int strlen(const char *s);
int strcmp(const char *p, const char *q);

extern char boot_stack[];

#endif
