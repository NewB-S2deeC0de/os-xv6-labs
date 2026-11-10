#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#ifdef PGTBL_SOL
#include "riscv.h"
#endif
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
    if (addr + n > UTOP)
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


#ifdef LAB_PGTBL
int
sys_vmprint(void)
{
  struct proc *p;

  p = myproc();
  vmprint(p->pagetable);
  return 0;
}
#endif

#ifdef LAB_PGTBL
#define PGACCESS_MAXPAGES 512  // upper bound on pages per call

int
sys_pgaccess(void)
{
  uint64 base, ubuf;
  int npages;

  argaddr(0, &base);
  argint(1, &npages);
  argaddr(2, &ubuf);

  // reject unreasonable page counts (also bounds the kernel buffer below)
  if (npages <= 0 || npages > PGACCESS_MAXPAGES)
    return -1;

  // reject ranges that overflow or leave the user address space
  uint64 start = PGROUNDDOWN(base);
  if (start >= MAXVA || (uint64)npages * PGSIZE > MAXVA - start)
    return -1;

  struct proc *p = myproc();
  char mask[PGACCESS_MAXPAGES / 8];
  memset(mask, 0, sizeof(mask));

  for (int i = 0; i < npages; i++) {
    pte_t *pte = walk(p->pagetable, start + (uint64)i * PGSIZE, 0);
    if (pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0)
      return -1;  // unmapped or not a user page
    if (*pte & PTE_A) {
      mask[i / 8] |= 1 << (i % 8);
      *pte &= ~PTE_A;  // clear so the next call reports only new accesses
    }
  }
  // stale TLB entries could still have A set and hide future accesses
  sfence_vma();

  if (copyout(p->pagetable, p->sz, ubuf, mask, (npages + 7) / 8) < 0)
    return -1;
  return 0;
}
#endif

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kkill(pid);
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

#ifdef LAB_LOCK
uint64
sys_cpupin(void)
{
  struct proc *p = myproc();
  int cpu;

  argint(0, &cpu);
  if (cpu < 0 || cpu >= NCPU)
    return -1;
  acquire(&p->lock);
  p->pincpu = &cpus[cpu];
  release(&p->lock);
  return 0;
}
#endif
