#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

// struct file {
//   uint off; // File offset
// };

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

pte_t *walkpgdir(pde_t *pgdir, const void *va, int alloc);

void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);

  initlock(&tickslock, "time");
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  if(tf->trapno == T_SYSCALL){
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if(myproc()->killed)
      exit();
    return;
  }

  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER:
    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
    }
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE+1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;

  case T_PGFLT: {

    // get the faulting virtual address
    uint fault_addr = rcr2();
    struct proc *p = myproc();

    // check if the process exists
    if (!p){
      cprintf("trap: no process for page fault at 0x%x\n", fault_addr);
      panic("trap");
    }

    // walk the page directory and get the page table entry
    pte_t *pte = walkpgdir(p->pgdir, (void *)fault_addr, 0);

    // if the page is marked copy on write
    if (pte && (*pte & PTE_COW)) {
      uint pa = PTE_ADDR(*pte);
      char *mem = kalloc();

      if (!mem) {
        cprintf("trap: out of memory for copy-on-write\n");
        p->killed = 1;
        return;
      }

      // if the child wants to write, then copy the contents of the og page to the new page
      memmove(mem, (char *)P2V(pa), PGSIZE);

      // update the page table entry to point to the new page with write permissions
      *pte = V2P(mem) | PTE_FLAGS(*pte) | PTE_W;
      *pte &= ~PTE_COW;

      // flush the TLB to ensure changes to the page table are effectice
      lcr3(V2P(p->pgdir));

      // decrement the reference count of the original page
      decr_ref_count(pa / PGSIZE);
      if (get_ref_count(pa / PGSIZE) == 0) {
        kfree((char *)P2V(pa));
      }

      return;
    }

    // handle invalid writes to read only pages
    if (pte && (*pte & PTE_P) && !(*pte & PTE_W)) {
      cprintf("Segmentation Fault\n");
      p->killed = 1;
      return;
    }

    // handle lazy allocation
    for (int i = 0; i < p->num_mmaps; i++) {

      struct mmap_region *region = &p->mmaps[i];

      // check if the fault address falls within the region
      if (fault_addr >= region->start_addr && fault_addr < region->start_addr + region->length) {
        char *mem = kalloc();
        if (!mem) {
          cprintf("Lazy allocation failed: out of memory\n");
          p->killed = 1;
          return;
        }

        // zero out the allocated pages
        memset(mem, 0, PGSIZE);

        // check if the mapping is file-backed
        if (region->f) {
          int file_offset = fault_addr - region->start_addr;
          int bytes_to_read = min(PGSIZE, region->length - file_offset);
          
          // if (!region->f) {
          //   cprintf("Lazy allocation failed: invalid file descriptor\n");
          //   kfree(mem);
          //   p->killed = 1;
          //   return;
          // }

          uint old_off = region->f->off;
          region->f->off = file_offset;

          int n = fileread(region->f, mem, bytes_to_read);

          // read the file content into memory
          if (n != bytes_to_read) {
            cprintf("Lazy allocation failed: file read error\n");
            kfree(mem);
            region->f->off = old_off;
            p->killed = 1;
            return;
          }

          region->f->off = old_off;

        }

        // use walkpgdir to ensure that the page table exists
        pte_t *pte = walkpgdir(p->pgdir, (void *)PGROUNDDOWN(fault_addr), 1);
        if (!pte) {
          cprintf("Lazy allocation failed: page table alloc failed\n");
          kfree(mem);
          p->killed = 1;
          return;
        }

        // map the allocated page at the fault address
        *pte = V2P(mem) | PTE_P | PTE_W | PTE_U;

        // increment the loaded page count in the region
        region->loaded_pages++;

        return;
      }
    }

    // if no matching mapping is found, then its an invalid accesss
    cprintf("Segmentation Fault\n");
    p->killed = 1;
    return;
  }
 
  //PAGEBREAK: 13
  default:
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER)
    yield();

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}
