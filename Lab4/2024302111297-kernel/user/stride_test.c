// stride_test.c — 步长调度断言(适用 LAB4_POLICY_ID = 6、7)
//
// 两个纯计算子进程,票数 t1 = 5 + 学号 % 15、t2 = 3 × t1(setickets 覆盖默认票数,
// 含 ID 7 按进程名分配的默认值)。断言:
//   1. 总量比例 cpu2 / cpu1 ≈ 3(整数形式:cpu2 × 10 在 cpu1 × 24 与 cpu1 × 36 之间,即 ±20%);
//   2. 有界公平:每个采样窗口内两个进程都获得 CPU(步长调度不应出现整窗口冷落)。
// 子进程到截止时刻自行退出,不依赖 kill。
//
// 输出规范:RESULT stride_ratio = <cpu1>:<cpu2>,随后 PASS: stride 或 FAIL: ...

#include "common.h"

#define WINDOWS 6
#define WIN_TICKS 50
#define MARGIN 60

int main(void) {
  int t1 = PARAM_STRIDE_TICKETS_MIN;
  int t2 = PARAM_STRIDE_TICKETS_MIN * 3;

  setickets(500);                      // 父进程醒来后尽快获得 CPU 以完成采样
  unsigned long dl = deadline_after(WINDOWS * WIN_TICKS + MARGIN);

  int p1 = fork();
  if (p1 == 0) { setickets(t1); burn_until(dl); exit(0); }
  int p2 = fork();
  if (p2 == 0) { setickets(t2); burn_until(dl); exit(0); }
  ASSERT(p1 > 0 && p2 > 0, "fork 失败");

  ASSERT(delta_cpu(p1) == 0 && delta_cpu(p2) == 0,
         "schedstat 采样失败(schedstat 未实现或子进程不存在)");   // 建立基准
  int cpu1[WINDOWS], cpu2[WINDOWS];
  for (int w = 0; w < WINDOWS; w++) {
    sleep(WIN_TICKS);
    cpu1[w] = delta_cpu(p1);
    cpu2[w] = delta_cpu(p2);
  }
  kill(p1); kill(p2);                  // 可选
  wait(0); wait(0);

  int total1 = 0, total2 = 0;
  for (int w = 0; w < WINDOWS; w++) {
    ASSERT(cpu1[w] >= 0 && cpu2[w] >= 0, "采样期间子进程已退出");
    total1 += cpu1[w]; total2 += cpu2[w];
  }
  printf("RESULT stride_ratio = %d:%d\n", total1, total2);
  ASSERT(total1 + total2 > WINDOWS * WIN_TICKS / 2, "总 CPU 采样过少,调度或 cpu_ticks 计数疑似未生效");

  ASSERT(total2 * 10 > total1 * 24 && total2 * 10 < total1 * 36,
         "CPU 分配比与票数比(1:3)偏差超过 ±20%");
  for (int w = 0; w < WINDOWS; w++)
    ASSERT(cpu1[w] > 0 && cpu2[w] > 0, "存在整窗口未被调度的进程(pass 有界性被破坏)");

  PASS("stride");
  return 0;
}
