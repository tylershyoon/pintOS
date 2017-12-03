#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <stdio.h>
#include <string.h>
#include "threads/synch.h"
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* On-disk inode.
   Must be exactly DISK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    //disk_sector_t start;                /* First data sector. */
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    bool is_dir;
    disk_sector_t d_indirect_sector;
    disk_sector_t parent;
    uint32_t unused[123];               /* Not used. */
  };

struct d_indirect_disk
{
  disk_sector_t indirect_sector[128];
};

struct indirect_disk
{
  disk_sector_t direct_sector[128];
};

struct lock inode_lock;
static void inode_alloc (struct inode_disk *);
static void inode_expand (struct inode_disk *, off_t);
static void indirect_expand (disk_sector_t, size_t, size_t);
static void dealloc_indirect (disk_sector_t, size_t);
static void dealloc_inode (struct inode_disk *);
static void inode_acquire ();
static void inode_release ();


/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, DISK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    disk_sector_t sector;               /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
  };

bool is_removed (struct inode *inode)
{
  return inode->removed;
}
/* Returns the disk sector that contains byte offset POS within
   INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static disk_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);

  int di = pos / DISK_SECTOR_SIZE;
  int indi = di / 128;

  struct d_indirect_disk *di_disk = malloc (DISK_SECTOR_SIZE);
  struct indirect_disk *i_disk = malloc (DISK_SECTOR_SIZE);

  disk_sector_t indirect_sector;
  disk_sector_t direct_sector;

  if (pos >= inode->data.length)
  {
    free (di_disk);
    free (i_disk);
    return -1;
  }

  di = di & 127;
  disk_read (filesys_disk, inode->data.d_indirect_sector, di_disk, true);
  indirect_sector = di_disk->indirect_sector[indi];
  disk_read (filesys_disk, indirect_sector, i_disk, true);
  direct_sector = i_disk->direct_sector[di];
  free (di_disk);
  free (i_disk);
  return direct_sector;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  lock_init (&inode_lock);
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   disk.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */

static void inode_acquire (void)
{
  if (!lock_held_by_current_thread (&inode_lock))
    lock_acquire(&inode_lock);
}

static void inode_release (void)
{
  if (lock_held_by_current_thread (&inode_lock))
    lock_release (&inode_lock);
}

static void inode_alloc (struct inode_disk *disk_inode)
{
  if(!free_map_allocate (1, &disk_inode->d_indirect_sector)) { return; }
  static char zeros[DISK_SECTOR_SIZE];
  disk_write (filesys_disk, disk_inode->d_indirect_sector, zeros, true);
  off_t newlength = disk_inode->length;
  disk_inode->length = 0;
  inode_expand (disk_inode, newlength);
}

static void inode_expand (struct inode_disk *disk_inode, off_t newlength)
{
  struct d_indirect_disk *d = malloc (DISK_SECTOR_SIZE);
  static char zeros[DISK_SECTOR_SIZE];
  off_t oldlength = disk_inode->length;
  int old_ind = DIV_ROUND_UP (bytes_to_sectors (oldlength), 128);
  int new_ind = DIV_ROUND_UP (bytes_to_sectors (newlength), 128);
  int old_d = bytes_to_sectors (oldlength) & 127;
  old_d = old_d ? old_d : 128;
  int new_d = bytes_to_sectors (newlength) & 127;
  new_d = new_d ? new_d : 128;

  if (oldlength == newlength)
  {
    free (d);
    return;
  }

  disk_read (filesys_disk, disk_inode->d_indirect_sector, d, true);

  if (old_ind == new_ind)
  {
    int secno = d->indirect_sector[new_ind - 1];
    indirect_expand (secno, old_d, new_d);
  }
  else
  {
    int i;
    for (i = old_ind;i < new_ind;i++)
    {
      free_map_allocate (1, &d->indirect_sector[i]);
      disk_write (filesys_disk, d->indirect_sector[i], zeros, true);
    }
    if (old_ind != 0){
      indirect_expand (d->indirect_sector[old_ind - 1], old_d, 128);
    }
    else {
      for(i = old_ind; i < new_ind - 1;i++){
        indirect_expand (d->indirect_sector[i], 0, 128);
      }
    }
    indirect_expand (d->indirect_sector[new_ind - 1], 0, new_d);
  }
  disk_inode->length = newlength;
  disk_write (filesys_disk, disk_inode->d_indirect_sector, d, true);
  free (d);
}
static void indirect_expand (disk_sector_t sector, size_t before, size_t after)
{
  struct indirect_disk *d = NULL;
  static char zeros[DISK_SECTOR_SIZE];
  if (before == after) return;
  d = malloc (DISK_SECTOR_SIZE);
  disk_read (filesys_disk, sector, d, true);

  for (;before < after;before++)
  {
    free_map_allocate (1, &d->direct_sector[before]);
    disk_write (filesys_disk, d->direct_sector[before], zeros, true);
  }
  disk_write (filesys_disk, sector, d, true);
  free (d);
}

static void dealloc_indirect (disk_sector_t sector, size_t sectors)
{
  struct indirect_disk *i_disk = malloc (DISK_SECTOR_SIZE);
  disk_read (filesys_disk, sector, i_disk, true);
  unsigned i;
  for(i = 0;i < sectors;i++){
    free_map_release (i_disk->direct_sector[i],1);
  }
  free (i_disk);
  free_map_release (sector, 1);
}

static void dealloc_inode (struct inode_disk *disk_inode)
{
  if (disk_inode->length == 0)
  {
    free_map_release (disk_inode->d_indirect_sector, 1);
    return;
  }

  struct d_indirect_disk *di_disk = malloc (DISK_SECTOR_SIZE);
  disk_read (filesys_disk, disk_inode->d_indirect_sector, di_disk, true);
  int sectors = bytes_to_sectors (disk_inode->length);
  int ind_sectors = DIV_ROUND_UP (sectors, 128);
  int i;

  sectors = sectors & 127;
  sectors = sectors ? sectors : 128;

  for (i = 0;i < ind_sectors-1;i++)
    dealloc_indirect (di_disk->indirect_sector[i], 128);

  dealloc_indirect (di_disk->indirect_sector[ind_sectors - 1], sectors);
  free_map_release (disk_inode->d_indirect_sector, 1);
  free (di_disk);
}

bool
inode_create (disk_sector_t sector, off_t length, bool dir)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == DISK_SECTOR_SIZE);
  inode_acquire ();
  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->is_dir = dir;
      inode_alloc (disk_inode);
      disk_write (filesys_disk, sector, disk_inode, true); 
      free (disk_inode);
      success = true;
    }
  inode_release ();
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (disk_sector_t sector) 
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  inode_acquire ();
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          inode_release ();
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
  {
    inode_release();
    return NULL;
  } 
  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  disk_read (filesys_disk, inode->sector, &inode->data, true);
  inode_release ();
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
disk_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  inode_acquire();
  
  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          dealloc_inode (&inode->data);
          free_map_release (inode->sector, 1);
        }
      else
        disk_write (filesys_disk, inode->sector, &inode->data, true);
      free (inode); 
    } 
  inode_release ();
}

void inode_close_all ()
{
  struct inode *inode;
  struct list_elem *e;

  for (e = list_begin (&open_inodes);e!=list_end(&open_inodes);e=list_next(e))
  {
    inode = list_entry (e, struct inode, elem);

    if (inode_get_inumber (inode) == 0 || inode_get_inumber (inode) == 1)
      return ;
    disk_write (filesys_disk, inode->sector, &inode->data, true);
    if (is_removed (inode))
    {
      dealloc_inode (&inode->data);
      free_map_release (inode->sector, 1);
    }
  }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

disk_sector_t inode_get_parent (struct inode *inode)
{
  if (inode->sector == 1) return 1;
  return inode->data.parent;
}

bool inode_isdir (struct inode *inode)
{
  return inode->data.is_dir;
}
/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;
  inode_acquire ();
  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      disk_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % DISK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = DISK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE) 
        {
          /* Read full sector directly into caller's buffer. */
          disk_read (filesys_disk, sector_idx, buffer + bytes_read, true); 
        }
      else 
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (DISK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          disk_read (filesys_disk, sector_idx, bounce, true);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);
  inode_release ();
  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;
  
  inode_acquire ();
  if (inode->data.length < offset + size)
  {
    inode_expand (&inode->data, offset + size);
    disk_write (filesys_disk, inode->sector, &inode->data, DISK_SECTOR_SIZE);
  }

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      disk_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % DISK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = DISK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == DISK_SECTOR_SIZE) 
        {
          /* Write full sector directly to disk. */
          disk_write (filesys_disk, sector_idx, buffer + bytes_written, true); 
        }
      else 
        {
          /* We need a bounce buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (DISK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left) 
            disk_read (filesys_disk, sector_idx, bounce, true);
          else
            memset (bounce, 0, DISK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          disk_write (filesys_disk, sector_idx, bounce, true); 
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);
  inode_release ();
  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}
void inode_set_parent (disk_sector_t parent, disk_sector_t child)
{
  struct inode *inode = inode_open (child);
  inode->data.parent = parent;
  inode_close (inode);
}
/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}
