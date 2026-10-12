/* 赠送件 · 不可改 —— 最小 shell(lab2)
 * 与 xv6 原版 sh 的差别:无参数解析、无重定向、无 cd——那些留给你
 * 的内核与后续实验去支撑。它只依赖 fork/exec/wait/read/write。
 * exec 的 ABI 是完整 xv6 签名 exec(name, argv);lab2 的内核实现
 * 允许忽略 argv(程序表里的程序都不需要参数)——这笔债 lab5 还。
 */
#include "kernel/types.h"
#include "user/user.h"

int main(void) {
  static char buf[64];

  while (1) {
    printf("sh> ");
    gets(buf, sizeof(buf));
    if (buf[0] == '\0')          /* 空行/EOF */
      continue;

    int pid = fork();
    if (pid < 0) {
      printf("fork failed\n");
      continue;
    }
    if (pid == 0) {
      char *argv[] = { buf, 0 };
      exec(buf, argv);
      printf("exec %s failed\n", buf);
      exit(1);
    }
    wait(0);
  }
}
