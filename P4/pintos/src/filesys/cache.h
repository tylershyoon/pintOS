#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H
/* added for utilizing disk_sector_t */
#include "devices/disk.h"

void cache_init (void);
void cache_read (disk_sector_t, void *);
void cache_write (disk_sector_t, void * );
void cache_flush (void);

#endif /* filesys/cache.h */
