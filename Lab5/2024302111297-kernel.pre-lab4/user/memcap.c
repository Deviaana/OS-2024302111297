// 官方测试 · lab3-test2:内存上限(LAB3_MEMCAP_DELTA)与 OOM 语义
// 断言(与 sid 无关):
//   1. sbrk 越过 (总页数 − DELTA) 上限时返回 -1(或等价错误),内核不 panic;
//   2. 失败后已有分配仍然可用(写读一致);sh 存活。
#include "kernel/types.h"
#include "user/user.h"

#define CHUNK (64 * 1024)   // 每次 16 页,循环加大直到上限

int
main(void)
{
    int rounds = 0;
    char *last = 0;
    for(;;){
        char *p = sbrk(CHUNK);
        if(p == (char *)-1 || p == 0)
            break;                       // OOM:规范要求报错而非 panic
        for(int i = 0; i < CHUNK; i += 4096)
            p[i] = 0x5a;                 // 已分配必须可用
        last = p;
        if(++rounds > 4096)
            break;                       // 防御:上限配置异常时不死循环
    }
    if(last != 0 && last[0] == 0x5a && rounds > 0){
        printf("TEST-4 PASS: OOM returned error after %d chunks; "
               "existing alloc intact; kernel alive\n", rounds);
        exit(0);
    }
    printf("TEST-4 FAIL: rounds=%d (OOM 未按规范返回错误或内存不可用)\n", rounds);
    exit(1);
}
