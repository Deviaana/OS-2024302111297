/* 预置支撑代码（请勿修改） —— 支持空格分词参数的最小 shell（lab5 升级版）
 * 在 lab2 最小 shell 基础上增加按空白字符切分命令行参数（最多 15 个参数），
 * 构造以 NULL 结尾的 argv[] 数组传给 exec(argv[0], argv)，用于配合 lab5 阶段四
 * 验证内核 exec 的 argc/argv 用户栈规整装配（例如在 sh> 下执行 echo hi）。
 */
#include "kernel/types.h"
#include "user/user.h"

#define MAXARGS 16

static int parse_args(char *s, char *argv[], int max) {
  int argc = 0;
  while (*s != '\0' && argc < max - 1) {
    while (*s == ' ' || *s == '\t')
      *s++ = '\0';
    if (*s == '\0')
      break;
    argv[argc++] = s;
    while (*s != '\0' && *s != ' ' && *s != '\t')
      s++;
  }
  argv[argc] = 0;
  return argc;
}

int main(void) {
  static char buf[64];
  char *argv[MAXARGS];

  while (1) {
    printf("sh> ");
    gets(buf, sizeof(buf));
    if (buf[0] == '\0')          /* 空行/EOF */
      continue;

    int argc = parse_args(buf, argv, MAXARGS);
    if (argc == 0)
      continue;

    int pid = fork();
    if (pid < 0) {
      printf("fork failed\n");
      continue;
    }
    if (pid == 0) {
      exec(argv[0], argv);
      printf("exec %s failed\n", argv[0]);
      exit(1);
    }
    wait(0);
  }
}
