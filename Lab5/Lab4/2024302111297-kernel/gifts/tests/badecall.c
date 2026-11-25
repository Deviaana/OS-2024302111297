// 官方测试 · lab2-test1:非法 ecall 必须返回 -1,不得 panic
// 用法:user-flat 内嵌后,在 sh 里执行 badecall;期望最后打出 "TEST-1 PASS"。
// 语义:内核对未知调用号返回 -1(sh 与内核均存活)。
#include "kernel/types.h"
#include "kernel/syscall.h"
#include "user/user.h"

// 直接发一个未定义调用号(99),绕过桩表 —— 桩表永远不含 99
static int
ecall_raw(int num, int a0)
{
    int ret;
    asm volatile("mv a7, %1\n"
                 "mv a0, %2\n"
                 "ecall\n"
                 "mv %0, a0"
                 : "=r"(ret) : "r"(num), "r"(a0) : "a7", "a0", "memory");
    return ret;
}

int
main(void)
{
    int bad = 0;
    for(int num = 90; num <= 99; num++)
        if(ecall_raw(num, 0) != -1)
            bad++;
    if(bad == 0){
        printf("TEST-1 PASS: unknown syscalls all return -1\n");
        exit(0);
    }
    printf("TEST-1 FAIL: %d unknown syscalls did not return -1\n", bad);
    exit(1);
}
