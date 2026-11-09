// 系统调用号表。
// 用户态桩函数（user/usys.pl 生成）把下面的编号放入 a7 后执行 ecall；
// 内核 syscall() 再以 a7 为下标查分发表，跳转到对应的 sys_xxx()。
#define SYS_fork   1
#define SYS_exit   2
#define SYS_wait   3
#define SYS_pipe   4
#define SYS_read   5
#define SYS_kill   6   // lab4 可选：设置目标进程的 killed 标志
#define SYS_exec   7
#define SYS_fstat  8
#define SYS_chdir  9
#define SYS_dup    10
#define SYS_getpid 11
#define SYS_sbrk   12
#define SYS_pause  13  // lab4：按时钟 tick 休眠，供测试控制观测窗口
#define SYS_uptime 14
#define SYS_open   15
#define SYS_write  16
#define SYS_mknod  17
#define SYS_unlink 18
#define SYS_link   19
#define SYS_mkdir  20
#define SYS_close  21
#define SYS_sync   22

// lab4 新增的调度观测/配置接口。
#define SYS_schedstat  23  // 读取指定进程的 CPU tick 与本次时间片
#define SYS_time_rdtime 24 // 读取硬件 time CSR 的计数值
#define SYS_setickets  25  // 设置当前进程票数，覆盖策略默认票数
