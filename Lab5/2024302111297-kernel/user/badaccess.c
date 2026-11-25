// 官方测试 · lab3-test1:非法访问被杀,内核完好
// 断言(与 sid 无关):
//   1. 写地址 0 / 写 MAXVA 附近 → 进程被杀(sbrk 分配后越界一页同样);
//   2. 父进程 wait 正常回收,内核存活(sh 仍在)。
#include "kernel/types.h"
#include "user/user.h"

static volatile int sink;

int
main(void)
{
    int pid = fork();
    if(pid == 0){
        // 页表开启后,地址 0 通常落在无映射/守卫区:写它必须被杀
        *(int *)(0) = 1;
        printf("TEST-3a FAIL: write to 0 survived\n");
        exit(0);                     // 不应走到这里
    }
    int st;
    wait(&st);
    if(st != 0)
        printf("TEST-3a PASS: wild write killed (status=%d)\n", st);
    else
        printf("TEST-3a FAIL: child exited normally after wild write\n");

    pid = fork();
    if(pid == 0){
        // 越过 sbrk 顶一页再写:同样必须被杀
        char *p = sbrk(4096);
        sink = p[8192];              // 越界一页读
        printf("TEST-3b FAIL: read beyond sbrk survived\n");
        exit(0);
    }
    wait(&st);
    if(st != 0)
        printf("TEST-3b PASS: beyond-sbrk access killed (status=%d)\n", st);
    else
        printf("TEST-3b FAIL: beyond-sbrk access survived\n");
    exit(0);
}
