// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"
#include "proc.h"

#define PA2PGREF_ID(p) (((p)-KERNBASE)/PGSIZE)
#define PGREF_MAX_ENTRIES PA2PGREF_ID(PHYSTOP)

struct spinlock pageref_lock;
int pageref[PGREF_MAX_ENTRIES];
#define PA2PGREF(p) pageref[PA2PGREF_ID((uint64)(p))]

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run
{
  struct run *next;
};

struct
{
  struct spinlock lock;
  struct run *freelist;
} kmem;

void kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&pageref_lock, "pageref");
  freerange(end, (void *)PHYSTOP);
}

void freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char *)PGROUNDUP((uint64)pa_start);
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa)
{
  struct run *r;

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  acquire(&pageref_lock);
  if(--PA2PGREF(pa) <= 0){

    memset(pa, 1, PGSIZE);
    r = (struct run *)pa;

    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
  }
  release(&pageref_lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if (r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if (r){
    memset((char *)r, 5, PGSIZE); // fill with junk
    PA2PGREF(r) = 1;
  }
  return (void *)r;
}

void *
kcopyderef(void *pa)
{
  acquire(&pageref_lock);

  if(PA2PGREF(pa) == 1){
    release(&pageref_lock);
    return pa;
  }

  uint64 pa_new = (uint64)kalloc();
  if(pa_new == 0){
    release(&pageref_lock);
    return 0;
  }

  memmove((void *)pa_new, pa, PGSIZE);
  PA2PGREF(pa)--;

  release(&pageref_lock);
  return (void *)pa_new;
}

int
kiscowpage(uint64 va)
{
  pte_t *pte;
  struct proc *p = myproc();
  return ((va < p->sz && ((pte = walk(p->pagetable, va, 0)) != 0) && (*pte & PTE_V) && (*pte & PTE_COW)));
}

void
kcopyinref(void *pa)
{
  acquire(&pageref_lock);
  PA2PGREF(pa)++;
  release(&pageref_lock); 
}

int
kcowcopy(uint64 va)
{
  pte_t *pte;
  struct proc *p = myproc();

  if((pte = walk(p->pagetable, va, 0)) == 0)
    panic("uvmcowcopy: walk");
  
  uint64 pa = PTE2PA(*pte);
  uint64 new = (uint64)kcopyderef((void*)pa); 
  if(new == 0)
    return -1;
  uint64 flags = (PTE_FLAGS(*pte) | PTE_W) & ~PTE_COW;
  uvmunmap(p->pagetable, PGROUNDDOWN(va), 1, 0);
  if(mappages(p->pagetable, va, 1, new, flags) == -1) {
    panic("uvmcowcopy: mappages");
  }
  return 0;
}