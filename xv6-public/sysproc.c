#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "wmap.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
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

int
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

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// insert the wmap and wunmap system calls here, include the wmap and wunmap files in the definitions and includes
// this one is the wmap system call
int
sys_wmap(void)
{
  uint addr;
  int length;
  int flags;
  int fd;

  // fetch the arguments for the wmap system call
  // if the address provided is invalid
  if (argint(0, (int*)&addr) < 0){
    return -1;
  }

  // if the length is less than 0, invalid length
  if (argint(1, &length) < 0){
    return -1;
  }

  // if the flags provided are invalid
  if (argint(2, &flags) < 0){
    return -1;
  }

  // if the file descriptors provided are invalid
  if (argint(3, &fd) < 0){
    return -1;
  }

  return wmap(addr, length, flags, fd);
}

// this one is the wunmap system call
int
sys_wunmap(void)
{
  uint addr;
  
  // if the address is invalid
  if (argint(0, (int*)&addr) < 0){
    return FAILED;
  }

  return wunmap(addr);
}

// the va2pa system call
int
sys_va2pa(void)
{
  uint va;
  if (argint(0, (int*)&va) < 0){
    return -1;
  }
  return (uint)va2pa(va);
}

// the getwmapinfo system call - new
int
sys_getwmapinfo(void)
{
  struct wmapinfo *uwminfo;
  struct wmapinfo kwminfo;
  struct proc *curproc = myproc();

  if (argptr(0, (void*)&uwminfo, sizeof(*uwminfo)) < 0){
    return FAILED;
  }

  kwminfo.total_mmaps = curproc->num_mmaps;
  for (int i = 0; i < kwminfo.total_mmaps; i++) {
    struct mmap_region *region = &curproc->mmaps[i];
    kwminfo.addr[i] = region->start_addr;
    kwminfo.length[i] = region->length;
    kwminfo.n_loaded_pages[i] = region->loaded_pages;
  }

  if (copyout(curproc->pgdir, (uint)uwminfo, &kwminfo, sizeof(kwminfo)) < 0) {
    return FAILED;
  }

  return SUCCESS;
}

