// lab2 自测用例(动手前先写好的两个用例的代码化):
//   用例 A:n == LAB2_BUF_SIZE(恰好等于缓冲区容量)的 read 不丢字、不越界
//   用例 B:空缓冲区上的 read 会阻塞,直到中断/轮询把字符送进缓冲区
//   用例 C:n == 1 的短读,验证"能交付多少就交付多少"
// 用法:在 sh 里执行 bufcap,按提示输入;或由 support/inject_uart.py 驱动。
#include "kernel/types.h"
#include "kernel/course_sid.h"
#include "user/user.h"

int
main(void)
{
    char buf[LAB2_BUF_SIZE];
    int n;

    printf("bufcap: A) type one line, read(n=%d)\n", (int)sizeof(buf));
    n = read(0, buf, sizeof(buf));
    printf("bufcap: A got=%d (must be 1..%d)\n", n, (int)sizeof(buf));
    if (n > 0)
        write(1, buf, n);

    printf("bufcap: B) type one char, read(n=1) on a possibly empty buffer\n");
    n = read(0, buf, 1);
    printf("bufcap: B got=%d\n", n);

    printf("bufcap: done\n");
    exit(0);
}