#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

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

  // added this case to set up the page fault handler to implement lazy allocation
  // so when you allocate pages using wmap, they wont actually be allocated in physical memory
  // but when you try to access the memory, this case will generate a page fault
  // this page fault will be handled by the kernel
  case T_PGFLT:
    
    // get the address that caused the page fault
    uint fault_addr = rcr2();
    struct proc *p = myproc();
    int i;

    // if page fault address is part of the mapping i.e. lazy allocation
    // iterate through all memory mapped regions to check if fault_addr is within a mapped region
    for (i = 0; i < p->num_mmaps; i++){
      struct mmap_region *region = &p->mmaps[i];
      
      // check if the fault_addr falls within the region
      if (fault_addr >= region->start_addr && fault_addr < region->start_addr + region->length){
        char *mem = kalloc();
        if (!mem) {
          cprintf("Lazy allocation failed: out of memory\n");
          // kill the process
          p->killed = 1;
          break;
        }

        // zero out the allocated pages
        memset(mem, 0, PGSIZE);

        // get the page directory and page table entries
        pde_t *pde = &p->pgdir[PDX(fault_addr)];
        pte_t *pgtab;

        if (*pde & PTE_P){
          pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
        }
        else {
          // allocate the page table if it doesnt exist
          if ((pgtab = (pte_t*)kalloc()) == 0){
            cprintf("Lazy allocation failed: page table alloc failed\n");
            kfree(mem);
            p->killed = 1;
            break;
          }
          
          memset(pgtab, 0, PGSIZE);
          *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;
        }

        pte_t *pte = &pgtab[PTX(fault_addr)];
        *pte = V2P(mem) | PTE_P | PTE_W | PTE_U;

        region->loaded_pages++;
        break;

      }
    }

    // else:
      // cprintf("Segmentation Fault\n");
      // kill the process
    if (i == p->num_mmaps){
      cprintf("Segmentation Fault\n");
      p->killed = 1;
    }
    
    break;
 
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
