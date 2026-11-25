/* 赠送件 · 不可改 —— 冒烟用户程序(lab2)
 * 用途:验证"内嵌程序表 → exec → U 态运行 → 系统调用返回"全链路。
 */
#include "kernel/types.h"
#include "user/user.h"

int main(void) {
  printf("hi: user program running, pid=%d\n", getpid());
  exit(0);
}
