#include "lib/string.h"
#include "lib/kernel/list.h"
#include <stdbool.h>
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "devices/disk.h"
#include "filesys/filesys.h"
#include "filesys/cache.h"

struct cache_block
{
  disk_sector_t sec_no;
  char buffer[DISK_SECTOR_SIZE];
  bool is_dirty;
  struct list_elem elem;
};

/* static Helpers */
/* function declarations using cache_block struct either input or output */
static struct cache_block* find_cache (disk_sector_t);
static struct cache_block* cache_evict (void);
static void block_flush (struct cache_block*);

/* list and lock for cache system */
struct lock cache_lock;
struct list cache_lst;

/* struct disk for holding filesys_disk*/
struct disk* disk;


void 
cache_init (void)
{
  disk = filesys_disk; /* disk used for file system - from filesys.h */
  struct cache_block* block;
  lock_init(&cache_lock);
  list_init(&cache_lst);
  int i;
  for (i=0; i<64; i++)
  {
    block = (struct cache_block *)malloc(sizeof(struct cache_block));
    block->sec_no = (disk_sector_t) - 1;
    block->is_dirty = false;
    list_push_back (&cache_lst, &block->elem);
  }
}

void
cache_read (disk_sector_t sec_no, void * buffer)
{
  lock_acquire(&cache_lock);
  struct cache_block* cb = find_cache(sec_no);
  if (!cb)
  {
    cb = cache_evict();
    cb->sec_no = sec_no;
    disk_read (disk, sec_no, cb->buffer, false);
  }
  memcpy (buffer, cb->buffer, DISK_SECTOR_SIZE);
  lock_release(&cache_lock);
}

void
cache_write (disk_sector_t sec_no, void * buffer)
{
  lock_acquire(&cache_lock);
  struct cache_block* cb = find_cache(sec_no);
  if(!cb)
  {
    cb = cache_evict();
    cb->sec_no = sec_no;
  }
  cb->is_dirty = true;
  memcpy(cb->buffer, buffer, DISK_SECTOR_SIZE);
  lock_release(&cache_lock);
}

void
cache_flush (void)
{
  struct cache_block *cb;
  struct list_elem* itr;
  lock_acquire(&cache_lock);

  for (itr = list_begin(&cache_lst); itr != list_end(&cache_lst); itr = list_next(itr))
  {
    cb = list_entry(itr, struct cache_block, elem);
    if (cb->is_dirty == true){ block_flush(cb); }
  }
  lock_release(&cache_lock);
}

/* Helpers: find, evict, block_flush */

static struct cache_block* find_cache (disk_sector_t sec_no)
{
  struct cache_block* cb;
  struct list_elem * itr;

  for (itr = list_rbegin(&cache_lst); itr != list_rend(&cache_lst); itr = list_prev(itr))
  {
    cb = list_entry (itr, struct cache_block, elem);
    if (cb->sec_no == sec_no)
    {
      list_remove(&cb->elem);
      list_push_back(&cache_lst, &cb->elem);
      return cb;
    }
    else if (cb->sec_no == (disk_sector_t) - 1 ){ return NULL; }
  }
  return NULL;
}

static struct cache_block* cache_evict (void)
{
  struct cache_block* cb = list_entry (list_pop_front(&cache_lst), struct cache_block, elem);
  if (cb->is_dirty)
  {
    block_flush (cb);
    memset(cb->buffer, 0, DISK_SECTOR_SIZE);
  }
  list_push_back(&cache_lst, &cb->elem);
  return cb;
}

static void block_flush (struct cache_block *cb)
{
  disk_write(disk, cb->sec_no, cb->buffer, false);
  cb->is_dirty = false;
}
