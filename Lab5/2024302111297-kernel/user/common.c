// common.c — lab4 调度测试的采样与负载框架(课程组提供,随包交付,请勿修改)
// 全部经 schedstat(pid, ...) / time_rdtime / pause 观测与计时,不进入内核数据结构。
#include "common.h"

static struct { int pid; int prev; int init; } T[16];
volatile unsigned long lab4_sink;     // 计算结果写入全局 volatile,防止编译器删除负载循环

int
cpu_of(int pid)
{
  int cpu, slice;
  if(schedstat(pid, &cpu, &slice) != 0)
    return -1;
  return cpu;
}

int
delta_cpu(int pid)
{
  int cpu, slice;
  if(schedstat(pid, &cpu, &slice) != 0)
    return -1;
  for(int i = 0; i < 16; i++){
    if(!T[i].init){
      T[i].pid = pid; T[i].prev = cpu; T[i].init = 1;
      return 0;                       // 首次调用:建立基准
    }
    if(T[i].pid == pid){
      int d = cpu - T[i].prev;
      T[i].prev = cpu;
      return d;
    }
  }
  return -1;
}

unsigned long
tick_cycles(void)
{
  pause(1);                           // 对齐到 tick 边界
  unsigned long t0 = time_rdtime();
  pause(10);
  unsigned long t1 = time_rdtime();
  unsigned long c = (t1 - t0) / 10;
  return c ? c : 1;
}

unsigned long
deadline_after(int ticks)
{
  static unsigned long per_tick = 0;
  if(per_tick == 0)
    per_tick = tick_cycles();
  return time_rdtime() + (unsigned long)ticks * per_tick;
}

// 纯计算负载:内层循环不发起系统调用,保证时钟中断大多命中用户态;
// 每段计算后读一次时间,判断是否到截止时刻。
void
burn_until(unsigned long deadline)
{
  while(time_rdtime() < deadline){
    for(int i = 0; i < 200000; i++)
      lab4_sink += i;               // 每次迭代读写 volatile,编译器不能化简或删除循环
  }
}

// 混合负载:短计算 + 主动休眠 1 tick(I/O 型进程的近似)
void
mixed_until(unsigned long deadline)
{
  while(time_rdtime() < deadline){
    for(int i = 0; i < 20000; i++)
      lab4_sink += i;
    pause(1);
  }
}
