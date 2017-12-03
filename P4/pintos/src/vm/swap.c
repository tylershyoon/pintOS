#include "vm/swap.h"
#include "devices/disk.h"
#include "lib/kernel/bitmap.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include <stdio.h>

struct bitmap *bmap;
struct disk *swap_disk;
struct lock swap_lock;

#define sector_number PGSIZE/DISK_SECTOR_SIZE

void
swap_init (void)
{
  swap_disk = disk_get(1,1);
  bmap = bitmap_create(disk_size(swap_disk));
  lock_init(&swap_lock);
}

void
swap_in (uint32_t sector, void *page)
{

  int i;
  if (page)
  {
    for (i=0; i<sector_number; i++)
    {
      disk_read(swap_disk, sector+i, page, false);
      page = page + DISK_SECTOR_SIZE;
    }
    bitmap_set_multiple(bmap, sector, sector_number, 0);
  }
}

uint32_t
swap_out (void *page)
{
  lock_acquire(&swap_lock);
  uint32_t base = bitmap_scan_and_flip(bmap, 0, sector_number, 0);
  int end = base + sector_number;
  int i;

  if (base==BITMAP_ERROR){ return BITMAP_ERROR; }
  for (i = base; i < end; i++)
  {
    disk_write(swap_disk, i, page, false);
    page = page + DISK_SECTOR_SIZE;
  }

  lock_release(&swap_lock);
  return base;
}


