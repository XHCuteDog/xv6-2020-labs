#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

#include "sysinfo.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_trace(void)
{
  int mask;
  if (argint(0, &mask) < 0) {
    return -1;  // 错误处理：无法从用户空间获取参数
  }
  myproc()->trace_mask = mask;  // 将mask存储在proc结构体的trace_mask变量中
  return 0;  // 成功执行，返回0
}

uint64
sys_sysinfo(void)
{
  // remember to change defs.h to ensure the kfreemem() and procnum() predeclare.
  struct sysinfo info;  // 在内核栈上分配一个 sysinfo 结构体
  uint64 up;  // 用户空间的 sysinfo 结构体指针

  // 获取用户空间的 sysinfo 结构体指针
  if(argaddr(0, &up) < 0){
    return -1;
  }

  // 获取系统信息
  info.freemem = kfreemem();
  info.nproc = procnum();

  // 将内核空间的 sysinfo 结构体复制到用户空间
  if(copyout(myproc()->pagetable, up, (char*)&info, sizeof(info)) < 0) {
    return -1;
  }

  return 0;
}

