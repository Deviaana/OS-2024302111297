// 官方测试 · lab2-test2:输入缓冲语义边界(行缓冲/字符流通用)
// 配合 support/inject_uart.py 使用:
//   sh 里执行 bufstorm;随后 inject 灌入 REPEAT 长 storm 行。
// 断言(与 sid 无关的通用不变式):
//   1. 读到的总字节数 == 注入的有效字节数(无静默丢字后的计数错乱);
//   2. 无论行缓冲还是字符流,回显完整、内核不 panic。
#include "kernel/types.h"
#include "user/user.h"

#define ROUND 4

int
main(void)
{
    char buf[256];
    int total = 0, lines = 0;
    // 从标准输入读 ROUND 行(inject_uart 逐行灌入)
    for(int i = 0; i < ROUND; i++){
        int n = read(0, buf, sizeof(buf));
        if(n <= 0)
            break;
        total += n;
        lines++;
        write(1, buf, n);       // 回显,供 EXPECT 校验完整性
    }
    printf("BUFSTORM lines=%d bytes=%d\n", lines, total);
    if(lines == ROUND)
        exit(0);
    exit(1);
}
