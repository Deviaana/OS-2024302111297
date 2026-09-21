/* 本文件自动生成,请勿修改;个性化参数唯一来源。 */
#ifndef COURSE_SID_H
#define COURSE_SID_H

#define COURSE_SID 2024302111297

#define LAB1_BANNER_PROTOCOL 1  /* 0=明文+换行 1=每字节后'.' 2=整段后回显校验和 */
#define LAB1_STACK_KB 8  /* 内核栈大小(KB) */
#define LAB2_TICK 2  /* 时间片 tick */
#define LAB2_BUF_SEMANTICS 1  /* 0=行缓冲 1=字符流 */
#define LAB2_BUF_SIZE 64  /* 输入缓冲区字节 */
#define LAB3_ALLOC_ORDER 1  /* 0=升序 1=降序 2=中点 */
#define LAB3_GUARD_PAGES 2  /* 每进程 guard 页数 */
#define LAB3_MEMCAP_DELTA 1  /* 内存上限 = 总页数 − 本值 */
#define LAB4_POLICY_ID 7  /* 分配表随 lab4 增量包发放(0=MLFQ 1=彩票 2=步长 3=优先级…) */
#define LAB5_REFCNT_SCHEME 1  /* 0=PTE保留位 1=全局数组 2=页表扫描 */
#define LAB5_COW_BUDGET 193  /* 缺页复制预算(次) */
#define LAB5_KILL_SEMANTICS 1  /* 0=立即不复制 1=完成当前复制 */
#define LAB6_REPLACE_POLICY 1  /* 0=LRU 1=FIFO 2=CLOCK 3=LFU */
#define LAB6_BUF_SLOTS 9  /* 缓存槽位数 */
#define LAB6_LOG_BLOCKS 25  /* 日志块数 */

#endif
