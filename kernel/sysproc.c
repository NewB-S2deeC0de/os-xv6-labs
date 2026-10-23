#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "syscall.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
  return 0; // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return kfork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int t;
  int n;

  argint(0, &n);
  argint(1, &t);
  addr = myproc()->sz;

  if (t == SBRK_EAGER || n < 0) {
    if (growproc(n) < 0) {
      return -1;
    }
  } else {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if (addr + n < addr)
      return -1;
    if (addr + n > TRAPFRAME)
      return -1;
    myproc()->sz += n;
  }
  return addr;
}

uint64
sys_pause(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if (n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n) {
    if (killed(myproc())) {
      release(&tickslock);
      return -1;
    }
    sleep_prepare(&ticks);
    release(&tickslock);
    sleep();
    acquire(&tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kkill(pid);
}

// Add syscall restrictions and an optional pathname exception for open/exec.
// Restrictions are monotone: a sandboxed process cannot widen its own permissions.
uint64
sys_interpose(void)
{
  int mask;
  char path[MAXPATH];
  int path_calls = (1 << SYS_open) | (1 << SYS_exec);

  argint(0, &mask);
  if (argstr(1, path, sizeof(path)) < 0)
    return -1;

  struct proc *p = myproc();

  // Rule 1: Once SYS_interpose itself is blocked, the process cannot call it
  // again to widen any permission.  Enforce monotonic restriction:
  // a process that is already sandboxed on interpose must not be able to
  // modify its own sandbox through this syscall.
  if (p->syscall_mask & (1 << SYS_interpose))
    return -1;

  // Rule 2: Mask bits may only be set, never cleared.
  p->syscall_mask |= mask;

  // Rule 3: Path restrictions may only be narrowed, never widened.
  // If a path filter is already set for a call, the new call cannot replace it
  // with a different (potentially broader) path.
  if (strncmp(path, "-", 2) == 0) {
    // "-" means no pathname exception: add to path_mask (blocking all paths).
    // We must not clear bits already in path_mask — that would re-allow a path.
    p->syscall_path_mask |= mask & path_calls;
  } else if (mask & path_calls) {
    // A specific path exception is requested.
    // Only allow setting the path if no path has been set yet for these calls;
    // a sandboxed process cannot replace an existing path restriction with a new one.
    int affected = mask & path_calls & ~p->syscall_path_mask;
    if (affected) {
      // First time setting a path filter for these calls: record the path.
      safestrcpy(p->syscall_path, path, sizeof(p->syscall_path));
      p->syscall_path_mask |= affected;
    }
    // Calls already in path_mask keep their original path — no change.
  }
  return 0;
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
