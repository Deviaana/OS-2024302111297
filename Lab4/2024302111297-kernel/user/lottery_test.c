// lottery_test.c — 彩票调度份额断言(适用 LAB4_POLICY_ID = 3、4、5)
//
// 断言:两个纯计算子进程分别以 setickets(t1)、setickets(t2) 设票后,
//       观测窗口内 CPU 占比 c1/(c1+c2) 与 t1/(t1+t2) 之差不超过 ±15 个百分点。
// setickets 语义(三个变体统一):设定后覆盖本进程的默认票数
//   (含 ID 3 的静态默认值、ID 5 按进程名分配的默认值;ID 4 的动态加票在此基础上进行,
//    纯计算进程不主动让出,票数保持不变)。
// 子进程到截止时刻自行退出,不依赖 kill。
//
// 输出规范:RESULT lottery_ratio = <c1>:<c2>,随后 PASS: lottery 或 FAIL: ...

#include "common.h"

#define RUN_TICKS 200      // 观测窗口
#define MARGIN    60       // 子进程在窗口结束后继续存活的余量,保证父进程采样时子进程仍在

// 默认票数 60:20(期望占比 75%):与轮转调度的 50% 相差 25 个百分点,轮转实现不会误判通过。
int main(int argc, char *argv[]) {
  int t1 = argc > 1 ? atoi(argv[1]) : 60;
  int t2 = argc > 2 ? atoi(argv[2]) : 20;

  setickets(500);                      // 父进程醒来后尽快获得 CPU 以完成采样
  unsigned long dl = deadline_after(RUN_TICKS + MARGIN);

  int p1 = fork();
  if (p1 == 0) { setickets(t1); burn_until(dl); exit(0); }
  int p2 = fork();
  if (p2 == 0) { setickets(t2); burn_until(dl); exit(0); }
  ASSERT(p1 > 0 && p2 > 0, "fork 失败");

  int b1 = cpu_of(p1), b2 = cpu_of(p2);
  sleep(RUN_TICKS);
  int c1 = cpu_of(p1), c2 = cpu_of(p2);
  kill(p1); kill(p2);                  // 可选:未实现 kill 时子进程到截止时刻自行退出
  wait(0); wait(0);

  ASSERT(b1 >= 0 && b2 >= 0 && c1 >= 0 && c2 >= 0,
         "schedstat 采样失败(子进程已退出或 schedstat 未实现)");
  c1 -= b1; c2 -= b2;
  printf("RESULT lottery_ratio = %d:%d\n", c1, c2);
  ASSERT(c1 + c2 > RUN_TICKS / 2, "总 CPU 采样过少,调度或 cpu_ticks 计数疑似未生效");

  int share  = c1 * 100 / (c1 + c2);   // 百分比,整数运算
  int expect = t1 * 100 / (t1 + t2);
  ASSERT(share > expect - 15 && share < expect + 15,
         "CPU 份额与票数比例偏差超过 ±15 个百分点");

  PASS("lottery");
  return 0;
}
