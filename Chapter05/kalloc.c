// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  char lock_name[10];
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];

void
kinit()
{
  // 初始化每个CPU对应的内存链表
  for (int i=0; i<NCPU; i++){
    snprintf(kmem[i].lock_name,10,"kmem_%d",i);
    initlock(&(kmem[i].lock), kmem[i].lock_name);
  }

  freerange(end, (void*)PHYSTOP);
}

// 操作系统内核启动，首次分配物理内存到链表
// 我们使用这个函数，目的是可以让物理内存平均分配给每个CPU的链表
static void kfree_first(void *pa, int cpu_index)
{
  struct run *r;
  
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&(kmem[cpu_index].lock));
  r->next = kmem[cpu_index].freelist;
  kmem[cpu_index].freelist = r;
  release(&(kmem[cpu_index].lock));
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  int i = 0;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
    i++;
    kfree_first(p, i % NCPU);  // 把所有物理内存平均分配给每个CPU
  }
}


// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
// 释放对应CPU的内存
void
kfree(void *pa)
{
  struct run *r;

    // 获取当前CPU
  int cpu_index = cpuid();

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&(kmem[cpu_index].lock));
  r->next = kmem[cpu_index].freelist;
  kmem[cpu_index].freelist = r;
  release(&(kmem[cpu_index].lock));
}

// 从指定的CPU分配内存
void* __kalloc(int cpu_index)
{
  struct run *r;

  acquire(&(kmem[cpu_index].lock));
  r = kmem[cpu_index].freelist;
  if(r)
    kmem[cpu_index].freelist = r->next;
  release(&(kmem[cpu_index].lock));

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}


void *
kalloc(void)
{
  void *r;

  // 获取当前CPU
  int cpu_id = cpuid();

  // 先从自己的 CPU 分配内存
  r = __kalloc(cpu_id);

  // 内存分配成功，返回
  if(r != (void*)0){
    return r;
  }

  // 内存分配失败，尝试从别的CPU分配内存
  for(int cpu_index =0; cpu_index < NCPU; cpu_index++){
    // 自己分配已经失败了，跳过自己
    if(cpu_index == cpu_id){
      continue;
    }

    // 从别的 CPU 分配内存
    r = __kalloc(cpu_index);
    if(r != (void*)0){
      return r;
    }

  }
  
  // 分配内存失败
  return (void*)0;
}
