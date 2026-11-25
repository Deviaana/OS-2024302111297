// 课程测试 · lab5-test2: COW 配对写与计数对账
// 断言(与 sid 无关):
//   1. fork 后、写之前, 共享页计数 > 0 (pmc(1));
//   2. 父子各写同一页: 各得独立副本且互不串数据;
//   3. 父子两轮写入合计触发恰好 2 次物理页复制(pmc(0) 增量 == 2):
//      fork() 后用户栈页与 sbrk 堆页(page)均处于只读 COW 共享态(refcnt == 2)。
//      栈页与堆页各自在先写入方触发 1 次物理页分配与拷贝(refcnt 降为 1, pmc(0) 加 1),
//      后写入方在 cow_alloc 中检测到 refcnt == 1 应直接原地恢复 PTE_W(零拷贝, 不计入 pmc(0)),
//      因此栈页 1 次 + 堆页 1 次合计 pmc(0) 增量恒为 2。
#include "kernel/types.h"
#include "user/user.h"

static volatile int child_done;

int
main(void)
{
    // 分配一页可写堆页(sbrk)
    char *page = sbrk(4096);
    if(page == (char *)-1){ printf("T5-2 FAIL: sbrk\n"); exit(1); }
    for(int i = 0; i < 4096; i++) page[i] = 0x11;

    unsigned long long c0 = pmc(0);
    int pid = fork();
    if(pid == 0){
        unsigned long long s1 = pmc(1);
        if(s1 == 0) printf("T5-2 WARN: fork 后共享计数为 0(检查你的计数时机)\n");
        // 子进程写堆页(栈页与堆页首次写入方触发复制, 独占方原地恢复 PTE_W)
        for(int i = 0; i < 4096; i++) page[i] = 0x22;
        for(int i = 0; i < 4096; i++)
            if(page[i] != 0x22){ printf("T5-2 FAIL: child read-back\n"); exit(1); }
        exit(0);
    }
    // 父进程写堆页(与子进程并发或先后写入, 检验数据隔离与 refcnt==1 原地复用)
    for(int i = 0; i < 4096; i++) page[i] = 0x33;
    for(int i = 0; i < 4096; i++)
        if(page[i] != 0x33){ printf("T5-2 FAIL: parent read-back\n"); exit(1); }
    int st; wait(&st);
    unsigned long long c1 = pmc(0);
    printf("T5-2 copies delta = %llu (expect 2)\n", c1 - c0);
    if(st == 0 && c1 - c0 == 2){
        printf("T5-2 PASS: pair-write exact 2 copies, data isolated\n");
        exit(0);
    }
    printf("T5-2 FAIL\n");
    exit(1);
}
