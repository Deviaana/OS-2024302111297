// bench.c — lab4 调度机制基准(所有策略均须通过)
//
// v2(2026-10):不再依赖管道。由两部分组成:
//   第一部分(往返延迟):父进程循环 ROUNDS 轮 "fork → 子进程立即 exit → 父进程 wait"。
//     每轮至少经历一次"父进程在 wait 中休眠 → 子进程 exit 唤醒父进程"的
//     sleep/wakeup 交接,用 time_rdtime 计时,输出单轮平均耗时。
//   第二部分(并发回收):一次性创建 NCHILD 个混合负载子进程(计算 + 主动休眠),
//     各自以不同退出码退出;父进程逐个 wait 并核对退出码齐全。
// 若存在丢失唤醒,父进程会在 wait 中永久休眠,表现为测试长时间无输出。
//
// 输出规范(自动评测与现场验收读取):
//   RESULT avg_fork_wait = <time_rdtime 计数/轮>
//   RESULT concurrent_children = <成功回收数>
//   PASS: bench

#include "common.h"

#define ROUNDS 100
#define NCHILD 8

int main(void) {
  // ---- 第一部分:fork/exit/wait 往返 ----
  unsigned long start = time_rdtime();
  for (int i = 0; i < ROUNDS; i++) {
    int pid = fork();
    ASSERT(pid >= 0, "fork 失败");
    if (pid == 0)
      exit(0);
    ASSERT(wait(0) == pid, "wait 返回的 pid 与刚创建的子进程不符");
  }
  unsigned long end = time_rdtime();
  printf("RESULT avg_fork_wait = %lu\n", (end - start) / ROUNDS);

  // ---- 第二部分:并发子进程回收 ----
  unsigned long dl = deadline_after(20);
  for (int i = 0; i < NCHILD; i++) {
    int pid = fork();
    ASSERT(pid >= 0, "fork 失败");
    if (pid == 0) {
      mixed_until(dl);
      exit(i + 1);                    // 退出码 1..NCHILD
    }
  }
  int seen = 0, got = 0;
  for (int i = 0; i < NCHILD; i++) {
    int st = -1;
    ASSERT(wait(&st) > 0, "并发子进程未能全部回收");
    ASSERT(st >= 1 && st <= NCHILD, "子进程退出码异常");
    seen |= 1 << st;
    got++;
  }
  ASSERT(seen == ((1 << (NCHILD + 1)) - 2), "退出码不齐全(存在重复回收或丢失)");
  printf("RESULT concurrent_children = %d\n", got);
  PASS("bench");
  return 0;
}
