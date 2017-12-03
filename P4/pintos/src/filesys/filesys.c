#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "devices/disk.h"
/* Added for buffer cache */
#include "filesys/cache.h"
/* Added for project 4 */
#include "threads/malloc.h"
#include "threads/thread.h"

/* The disk that contains the file system. */
struct disk *filesys_disk;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  filesys_disk = disk_get (0, 1);
  if (filesys_disk == NULL)
    PANIC ("hd0:1 (hdb) not present, file system initialization failed");

  inode_init ();
  free_map_init ();
  /* buffer cache init */
  cache_init ();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  inode_close_all();
  free_map_close ();
  cache_flush ();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size, bool is_dir) 
{
  disk_sector_t inode_sector = 0;
  /* Modified in project 4 */
  if (name[0] == 0){ return false; }
  struct dir *dir = dir_from_path (name, false);
  char* real_name = name_from_path (name);
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, is_dir)
                  && dir_add (dir, real_name, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);
  free(real_name);

  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  struct dir *dir = dir_from_path (name, false);
  char * real_name = name_from_path(name);
  if (real_name == NULL){ file_open(dir_get_inode(dir)); }
  struct inode *inode = NULL;

  if (dir != NULL)
    dir_lookup (dir, real_name, &inode);
  free(real_name);
  //dir_close (dir);

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  /* Modified in project 4 */
  if (!strcmp(name, ".") || !strcmp(name, "..")){ return false; }
  struct dir *dir = dir_from_path (name, false);
  if (!dir){ return false; }
  char * real_name = name_from_path (name);
  
  if (!real_name)
  {
    dir_close (dir);
    return false;
  }
  bool success = dir != NULL && dir_remove (dir, real_name);
  dir_close (dir); 
  free(real_name);

  return success;
}

/* Added for chdir in project 4 */
bool
filesys_chdir (const char* name)
{
  struct thread* curr = thread_current();
  struct dir * dir = dir_from_path(name, true); /* added under directory */

  if (dir)
  {
    if(curr->cwd){ dir_close(curr->cwd); }
    curr->cwd = dir_reopen(dir);
    return true;
  }
  return false;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}

/* Helper: get name from path */
char *
name_from_path (const char * path_given)
{
  size_t path_size = strlen(path_given)+1;
  char *curr, *next;
  char *save_ptr;
  char *path = malloc(path_size);
  char *ret;
  curr = NULL;
  memcpy(path, path_given, path_size);
  /* for loop flies to the end of the long path -> to get name */
  for (next = strtok_r(path, "/", &save_ptr); next != NULL; next = strtok_r(NULL, "/", &save_ptr))
  {
    curr = next;
  }
  if (!curr){ return NULL; }
  ret = malloc(strlen(curr) + 1); /* allocate enough space */
  memcpy(ret, curr, strlen(curr) + 1);
  free(path);
  return ret;
}
