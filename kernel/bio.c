// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUF_BUCKET 13
#define BUF_BUCKET_HASH(dev, blockno) ((17 * (dev) + (blockno)) % NBUF_BUCKET)

struct {
  struct spinlock evict_lock;
  struct buf buf[NBUF];

  // // Linked list of all buffers, through prev/next.
  // // Sorted by how recently the buffer was used.
  // // head.next is most recent, head.prev is least.
  // struct buf head;  // remove head by lab hint.........
  // create buckets to store buffers 
  struct buf buf_bucket[NBUF_BUCKET];
  struct spinlock buf_bucket_lock[NBUF_BUCKET];

} bcache;

void
binit(void)
{
  // initialize the bucket lock 
  for(int i = 0; i < NBUF_BUCKET; i++){
    initlock(&bcache.buf_bucket_lock[i], "bcache_bucket_lock");
    bcache.buf_bucket[i].next = 0;
  }

  // 对于每个buffer，初始化其lock，next，lastused，refcnt。
  // 其中lastused是根据lab hint添加的（instead time-stamp buffers using the time of their last use）
  // 要注意的是，一开始我们均匀将buf分散开来，这样可以避免某个bucket过于拥挤。
  // 思考：这样的设计真的对吗？
  for(int i = 0; i < NBUF; i++)
  {
    struct buf *b = &bcache.buf[i];
    initsleeplock(&b->lock, "buffer");
    b->refcnt = 0;
    b->lastused = 0;
    b->next = bcache.buf_bucket[i % NBUF_BUCKET].next;
    bcache.buf_bucket[i % NBUF_BUCKET].next = b;
  }
  initlock(&bcache.evict_lock, "evict_lock");
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  // 使用dev和blockno来计算bucket_key，这样可以保证同一个block的buffer一定在同一个bucket中。
  uint bucket_key = BUF_BUCKET_HASH(dev, blockno);
  acquire(&bcache.buf_bucket_lock[bucket_key]);
  // Is the block already cached?
  for(b = bcache.buf_bucket[bucket_key].next; b != 0; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.buf_bucket_lock[bucket_key]);
      // when we get the buffer, we should take its lock
      acquiresleep(&b->lock);
      return b;
    }
  }
  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  release(&bcache.buf_bucket_lock[bucket_key]);
  // 任何时候都必须保持“多线程”的思想，如果有其他线程也同时或者先后遇到了这个情况会出现什么情况？
  // 有两种情况：可能有一个更早的线程也在寻找这个buffer，所以他会创建这个，然后我们就可以直接使用了。
  // 如何确保我们的检查是晚于那个线程的呢？ 持有一个更粗粒度的锁或许会帮助我们解决这个问题
  // 另一种情况是，我们就是那个首先寻找的线程，所以当前也会找不到，如果找不到，那就是真的需要创建了
  acquire(&bcache.evict_lock);
  for(b = bcache.buf_bucket[bucket_key].next; b; b = b->next){
    if(b->blockno == blockno && b->dev == dev){
      acquire(&bcache.buf_bucket_lock[bucket_key]);
      b->refcnt++;
      release(&bcache.buf_bucket_lock[bucket_key]);
      release(&bcache.evict_lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  // 现在，我们就是真的在cache中不存在这个buffer了，所以我们需要创建一个。
  struct buf *prelrubuf = 0;
  uint bucket_key_lru = NBUF_BUCKET;
  for(int i = 0; i < NBUF_BUCKET; i++)
  {
    acquire(&bcache.buf_bucket_lock[i]);
    int morecapablefound = 0;
    for(b = &bcache.buf_bucket[i]; b->next; b = b->next){
      if(b->next->refcnt == 0 && (!prelrubuf || b->next->lastused < prelrubuf->next->lastused)){
        prelrubuf = b;
        morecapablefound = 1;
      }
    }
    if(morecapablefound)
    {
      if(bucket_key_lru != NBUF_BUCKET)
      {
        release(&bcache.buf_bucket_lock[bucket_key_lru]);
      } // 已经找到过一次了，所以需要释放掉之前所找到的那个bucket的锁
      bucket_key_lru = i;
      // 我们现在还持有这个更合适的bucket的锁。
    }
    else{
      release(&bcache.buf_bucket_lock[i]);
    }
  }
  if(!prelrubuf)
  {
    panic("bget: no buffers");
  }
  b = prelrubuf->next;
  // 现在，b就是我们要找的buffer了，我们需要将其从原来的bucket中移除，然后放到新的bucket中。
  if(bucket_key_lru != bucket_key)
  {
    // 我们需要修改找到的lrubuf前面的buffer的next指针，但是prev已经被我们删掉了，所以应该修改之前的代码
    // 使得我们找到的是最合适的buf的前一个才行
    prelrubuf->next = b->next;
    release(&bcache.buf_bucket_lock[bucket_key_lru]);

    // 现在，我们需要将其放到新的bucket中，也就是我们hash到的bucket
    acquire(&bcache.buf_bucket_lock[bucket_key]);
    b->next = bcache.buf_bucket[bucket_key].next;
    bcache.buf_bucket[bucket_key].next = b;
  }
  // 设置metadata
  b->dev = dev;
  b->blockno = blockno;
  b->valid = 0;
  b->refcnt = 1;
  // b->lastused = ticks; // 似乎并不需要更新这个，因为get的目的事实上就是为了操作这个块，只在操作的时候进行更新即可
  release(&bcache.buf_bucket_lock[bucket_key]);
  release(&bcache.evict_lock);
  acquiresleep(&b->lock);
  return b;
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;
  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);
  uint bucket_key = BUF_BUCKET_HASH(b->dev, b->blockno);
  acquire(&bcache.buf_bucket_lock[bucket_key]);
  b->refcnt--;
  if (b->refcnt == 0) {
    b->lastused = ticks;
  }
  
  release(&bcache.buf_bucket_lock[bucket_key]);
}

void
bpin(struct buf *b) {
  uint bucket_key = BUF_BUCKET_HASH(b->dev, b->blockno);
  acquire(&bcache.buf_bucket_lock[bucket_key]);
  b->refcnt++;
  release(&bcache.buf_bucket_lock[bucket_key]);
}

void
bunpin(struct buf *b) {
  uint bucket_key = BUF_BUCKET_HASH(b->dev, b->blockno);
  acquire(&bcache.buf_bucket_lock[bucket_key]);
  b->refcnt--;
  release(&bcache.buf_bucket_lock[bucket_key]);
}


