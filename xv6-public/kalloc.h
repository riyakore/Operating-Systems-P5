#ifndef KALLOC_H
#define KALLOC_H

#include "spinlock.h"

// Declaration of kmem
extern struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
} kmem;

// Declaration of ref_counts
extern uchar ref_counts[];

#endif // KALLOC_H
