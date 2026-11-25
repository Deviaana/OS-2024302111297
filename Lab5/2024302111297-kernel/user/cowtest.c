#include "kernel/types.h"
#include "kernel/course_sid.h"
#include "user/user.h"

static void
fail(const char *msg)
{
  printf("T5-SELF FAIL: %s\n", msg);
  exit(1);
}

static void
check_child(int pid, int status, const char *what)
{
  if (pid < 0 || status != 0) {
    printf("T5-SELF FAIL: %s (pid=%d status=%d)\n", what, pid, status);
    exit(1);
  }
}

static void
test_rounds(void)
{
  volatile uchar *page = (volatile uchar *)sbrk(4096);

  if (page == (volatile uchar *)-1)
    fail("rounds sbrk");

  for (int round = 0; round < 6; round++) {
    uchar parent_value = (uchar)(0x20 + round);
    uchar child_value = (uchar)(0x80 + round);

    page[0] = parent_value;
    int pid = fork();
    if (pid == 0) {
      if (page[0] != parent_value)
        exit(2);
      page[0] = child_value;
      for (int i = 0; i < 32; i++) {
        if (page[0] != child_value)
          exit(3);
      }
      exit(0);
    }
    if (pid < 0)
      fail("rounds fork");
    if (pmc(1) == 0)
      fail("rounds shared count did not rise");

    int status;
    int waited = wait(&status);
    check_child(waited, status, "rounds child");
    if (page[0] != parent_value)
      fail("rounds parent data was modified");
    if (pmc(1) != 0)
      fail("rounds shared pages leaked after wait");
  }
  printf("T5-SELF PASS: 6 alternating COW rounds\n");
}

static void
test_multi_child(void)
{
  volatile uchar *page = (volatile uchar *)sbrk(4096);
  int pids[3];

  if (page == (volatile uchar *)-1)
    fail("multi-child sbrk");
  page[0] = 0x11;

  for (int i = 0; i < 3; i++) {
    int pid = fork();

    if (pid == 0) {
      if (page[0] != 0x11)
        exit(2);
      page[0] = (uchar)(0x40 + i);
      if (page[0] != (uchar)(0x40 + i))
        exit(3);
      exit(0);
    }
    if (pid < 0)
      fail("multi-child fork");
    pids[i] = pid;
  }

  for (int i = 0; i < 3; i++) {
    int status;
    int waited = wait(&status);

    if (waited != pids[i])
      fail("multi-child wait order");
    check_child(waited, status, "multi-child");
  }
  if (page[0] != 0x11)
    fail("multi-child parent data was modified");
  if (pmc(1) != 0)
    fail("multi-child shared pages leaked");

  page[0] = 0x66;
  if (page[0] != 0x66)
    fail("multi-child parent restore");
  printf("T5-SELF PASS: 3-way sharing and shared count drained\n");
}

static void
test_readonly_write(void)
{
  int pid = fork();

  if (pid == 0) {
    *(volatile char *)0 = 0x7f;
    exit(0);
  }
  if (pid < 0)
    fail("readonly fork");

  int status;
  int waited = wait(&status);

  if (waited != pid || status != -1)
    fail("readonly write was not killed with -1");
  printf("T5-SELF PASS: read-only text write killed, kernel alive\n");
}

static void
test_budget(void)
{
  int pages = LAB5_COW_BUDGET + 8;
  volatile uchar *base = (volatile uchar *)sbrk(pages * 4096);

  if (base == (volatile uchar *)-1)
    fail("budget sbrk");
  for (int i = 0; i < pages; i++)
    base[i * 4096] = (uchar)i;

  int pid = fork();
  if (pid == 0) {
    for (int i = 0; i < pages; i++) {
      base[i * 4096] = (uchar)(0xa0 + (i & 0x1f));
      if (base[i * 4096] != (uchar)(0xa0 + (i & 0x1f)))
        exit(3);
    }
    exit(0);
  }
  if (pid < 0)
    fail("budget fork");

  int status;
  int waited = wait(&status);

  check_child(waited, status, "budget child");
  for (int i = 0; i < pages; i++)
    if (base[i * 4096] != (uchar)i)
      fail("budget parent data was modified");
  if (pmc(1) != 0)
    fail("budget shared pages leaked");
  printf("T5-SELF PASS: COW budget=%d continued after warning\n",
         LAB5_COW_BUDGET);
}

static void
test_exec(void)
{
  int pid = fork();

  if (pid == 0) {
    if (exec("echo", (char **)0x70000000UL) != -1)
      exit(2);
    exit(0);
  }
  if (pid < 0)
    fail("exec invalid-pointer fork");

  int status;
  int waited = wait(&status);

  check_child(waited, status, "exec invalid pointer accepted");

  pid = fork();
  if (pid == 0) {
    char *argv[] = { "echo", "cowtest", "exec", 0 };

    exec("echo", argv);
    exit(2);
  }
  if (pid < 0)
    fail("exec argv fork");
  waited = wait(&status);
  check_child(waited, status, "exec argv child");
  printf("T5-SELF PASS: exec rejects bad argv and forwards arguments\n");
}

int
main(void)
{
  printf("T5-SELF begin\n");
  test_rounds();
  test_multi_child();
  test_readonly_write();
  test_budget();
  test_exec();
  printf("T5-SELF ALL PASS\n");
  exit(0);
}
