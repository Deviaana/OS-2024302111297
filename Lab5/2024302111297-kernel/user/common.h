// common.h — lab4 调度测试公共设施(课程组提供,随包交付,请勿修改)
//
// v2(2026-10)修订要点:
//   1. 负载改为纯计算循环(v1 的负载实为 pause(1) 循环,几乎不占 CPU,
//      正确实现下 CPU 采样也不足,断言无法成立);
//   2. 子进程到截止时刻自行退出,不再依赖 kill(kill 缺失时测试报 FAIL 而非永久挂起);
//   3. 全部改用整数运算(用户态浮点指令在未开启 sstatus.FS 的内核上为非法指令)。
//
// 依赖的系统调用(学生内核须实现):fork/exit/wait/getpid/write、
//   pause(n)、schedstat(23)、time_rdtime(24)、setickets(25)。kill 可选。
// 语义见增量包 support/schedstat.patch 头部注释与 support/tests/README-tests.md。

#ifndef _LAB4_COMMON_H
#define _LAB4_COMMON_H

#include "kernel/types.h"
#include "user/user.h"

// 评测时通过 -DSID=<学号> 编译注入,断言参数据此计算
#ifndef SID
#define SID 0
#endif

#define PARAM_STRIDE_TICKETS_MIN (5 + SID % 15)

// 测试程序用 sleep(...) 命名;课程调用号表为 pause(见 syscall.h)
#define sleep(t) pause(t)

// ---- 采样记录框架(common.c 实现,统一经 schedstat 差分/汇总) ----
int   cpu_of(int pid);        // 目标进程累计 CPU tick(须在其存活期查询;失败返回 -1)
int   delta_cpu(int pid);     // 距上次调用的增量;首次调用建立基准并返回 0
unsigned long tick_cycles(void);  // 实测一个时钟 tick 对应的 time_rdtime 计数
unsigned long deadline_after(int ticks); // 当前时刻 + ticks 个 tick 对应的截止时刻
void  burn_until(unsigned long deadline);  // 纯计算负载,到截止时刻返回
void  mixed_until(unsigned long deadline); // 计算一小段后 pause(1),循环至截止时刻

#define ASSERT(cond, msg) do { \
  if (!(cond)) { \
    printf("FAIL: %s (at %s:%d)\n", msg, __FILE__, __LINE__); \
    exit(1); \
  } \
} while (0)

#define PASS(msg) do { printf("PASS: %s\n", msg); exit(0); } while (0)

#endif
