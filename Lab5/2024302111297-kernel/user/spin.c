/* 赠送件 · 不可改 —— 忙循环用户程序(lab2)
 * 用途:演示中断驱动控制台(内核输出与程序输出交错)、
 * 以及 lab4 之后的调度行为。无 sleep 可用(lab4 才有),用忙等降速。
 */
#include "kernel/types.h"
#include "user/user.h"

int main(void) {
  for (int i = 0; ; i++) {
    printf("spin %d\n", i);
    for (volatile int d = 0; d < 2000000; d++)
      ;
  }
}
