#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
/* for power off */
#include "threads/init.h"`
/* for filesys use */
#include "filesys/filesys.h"
#include "filesys/file.h"
/* for accessing user memory */
#include "threads/vaddr.h"
/* for lock structure usage */
#include "threads/synch.h"
/* open file using memory alloc */
#include "threads/malloc.h"
/* for impl mmap */
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "vm/page.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include "lib/kernel/hash.h"

static void syscall_handler (struct intr_frame *);

/* syscall regarding files */
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
void close (int fd);
int filesize (int fd);
int read (int fd, void *buffer, unsigned size);
void seek (int fd, unsigned position);
unsigned tell (int fd);

/* syscall regarding pid */
int exec (const char * cmd_line);
int wait (int pid);

/* syscall added in project 4 */
bool chdir (const char* path);
bool mkdir (const char* path);
bool readdir (int fd, char* name);
bool isdir (int fd);
int inumber (int fd);


/* few helper headers */
struct thread_file* thread_file_by_fd(int fd);

/* lock for accessing file - for synchronization purpose */
struct lock file_lock;

/* struct thread_file for thread to hold a list of files */
struct thread_file
{
  struct file * file;
  int fd;
  struct list_elem file_elem;
};
/* END of struct declaration */


/* few helper functions */
static void is_valid (const void *vaddr)
{
  if (!is_user_vaddr(vaddr) || vaddr == NULL || pagedir_get_page (thread_current ()->pagedir, vaddr)== NULL)
  { exit(-1); }
}

static struct file* file_by_fd(int fd, struct thread *t)
{
  struct list_elem *itr;
  struct thread_file* itrfile;
  struct list *file_list = &t->file_list;

  int size = list_size(file_list);
  int i = 0;
  if (size == 0){ return NULL; }
  for (itr = list_begin(file_list); i < size; ++i)
  {
    itrfile = list_entry(itr, struct thread_file, file_elem);
    if (itrfile->fd == fd){ return itrfile->file; }
    itr = list_next(itr);
  }
  return NULL;
}

void
syscall_init (void) 
{
  lock_init(&file_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  //printf ("system call!\n");

  int args[3] = {0,0,0};     /* For arguments - maximum three for syscalls */

  thread_current()->esp = f->esp;

  //if(!is_user_vaddr(f->esp)){ exit(-1); }
  //if(!is_user_vaddr(f->esp + 4) || !is_user_vaddr(f->esp + 8) || ! is_user_vaddr(f->esp + 12)){ exit(-1); }
  /* FIRST switch-case for confirming arguments */
  switch (*(int*)f->esp)
  {
    /* one-argument syscalls */
    case SYS_EXIT:
    case SYS_EXEC:
    case SYS_WAIT:
    case SYS_REMOVE:
    case SYS_OPEN:
    case SYS_FILESIZE:
    case SYS_TELL:
    case SYS_CLOSE:
    case SYS_MUNMAP:
    case SYS_CHDIR:
    case SYS_MKDIR:
    case SYS_ISDIR:
    case SYS_INUMBER:
      is_valid((const void*)(4 + f->esp));
      args[0] = *(int *)(4 + f->esp);
      break;
    /* two-argument syscalls */
    case SYS_CREATE:
    case SYS_SEEK:
    case SYS_MMAP:
    case SYS_READDIR:
      is_valid((const void*)(8 + f->esp));
      args[0] = *(int *)(4 + f->esp);
      args[1] = *(int *)(8 + f->esp);
      break;
    /* three-argument syscalls */
    case SYS_READ:
    case SYS_WRITE:
      is_valid((const void*)(12 + f->esp));
      args[0] = *(int *)(4 + f->esp);
      args[1] = *(int *)(8 + f->esp);
      args[2] = *(int *)(12 + f->esp);
      break;
  }

  /* SECOND switch-case for calling each syscalls */
  switch (*(int*)f->esp)
  {
    /* SYSCALL for working minimally */
    case SYS_HALT:
      halt();
      break;
    case SYS_EXIT:
      exit(args[0]);
      break;
    case SYS_WRITE:
      f->eax = write(args[0], (const void*)args[1], (unsigned int)args[2]);
      break;

    /* SYSCALL regarding files */
    case SYS_CREATE:
      if(args[0] == NULL){ exit(-1); }
      f->eax = create((const char*)args[0], (unsigned)args[1]);
      break;
    case SYS_REMOVE:
      f->eax = remove((const char*)args[0]);
      break;
    case SYS_OPEN:
      if(args[0] == NULL){ exit(-1); }
      f->eax = open((const char*)args[0]);
      break;
    case SYS_FILESIZE:
      f->eax = filesize((int)args[0]);
      break;
    case SYS_READ:
      f->eax = read((int)args[0],(void *)args[1],(unsigned)args[2]);
      break;
    case SYS_SEEK:
      seek((int)args[0], (unsigned)args[1]);
      break;
    case SYS_TELL:
      f->eax = tell((int)args[0]);
      break;
    case SYS_CLOSE:
      close((int)args[0]);
      break;
    /* SYSCALL regarding pid */
    case SYS_EXEC:
      f->eax = exec((const char *)args[0]);
      break;
    case SYS_WAIT:
      f->eax = wait((int)args[0]);
      break;
    case SYS_MMAP:
      f->eax = mmap(args[0], (void*)args[1]);
      break;
    case SYS_MUNMAP:
      munmap(args[0]);
      break;
    /* SYSCALL added in project 4 */
    case SYS_CHDIR:
      f->eax = chdir((char*)args[0]);
      break;
    case SYS_MKDIR:
      f->eax = mkdir((char*)args[0]);
      break;
    case SYS_READDIR:
      f->eax = readdir((int)args[0], (char *)args[1]);
      break;
    case SYS_ISDIR:
      f->eax = isdir(args[0]);
      break;
    case SYS_INUMBER:
      f->eax = inumber(args[0]);
      break;
  }

  //thread_exit ();
}

/* syscall functions */
void
halt (void)
{
  power_off();
}

void
exit (int status)
{
  int munmap_itr;
  struct thread* curr = thread_current();
  printf("%s: exit(%d)\n", thread_current()->name, status);
  /*struct thread * curr = thread_current();
  sema_down(&curr->exit_sema);
  curr->exit_status = status;
  struct list_elem* itr;

  while(!list_empty(&curr->file_list))
  {
    itr = list_pop_front(&curr->file_list);
    close(list_entry(itr, struct thread_file, file_elem)->fd);
  }

  if (curr->executable != NULL)
  {
    file_allow_write(curr->executable);
  }

  if (lock_held_by_current_thread(&file_lock))
  {
    lock_release(&file_lock);
  }*/

  struct list_elem* itr;
  struct thread* child;
  while (!list_empty(&curr->childs))
  {
    itr = list_front(&curr->childs);
    child = list_entry(itr, struct thread, child_elem);
    child->parent = NULL;
    list_pop_front(&curr->childs);
  }
  if(lock_held_by_current_thread(&file_lock))
  {
    lock_release(&file_lock);
  }
  for (munmap_itr = 0; munmap_itr <= curr->mapid; munmap_itr++)
  {
    munmap(munmap_itr);
  }
  while(!list_empty(&curr->file_list))
  {
    itr = list_pop_front(&curr->file_list);
    close(list_entry(itr, struct thread_file, file_elem)->fd);
  }
  if (curr->executable != NULL)
  {
    file_allow_write(curr->executable);
  }
  while(!list_empty(&curr->wait_sema.waiters))
  {
    sema_up(&curr->wait_sema);
  }

  curr->exit = 1;
  curr->exit_status = status;
  if(curr->parent)
  {
    sema_down(&curr->exit_sema);
  }
  thread_exit ();
}

int
exec (const char *cmd_line)
{
  is_valid(cmd_line);
  return process_execute(cmd_line);
}


int
wait (int pid)
{
  return process_wait(pid);
}

bool
create (const char *file, unsigned initial_size)
{
  is_valid(file);
  if(*file==0){ return 0; }
  lock_acquire(&file_lock);
  bool success = filesys_create (file, (off_t)initial_size, false);
  lock_release(&file_lock);
  return success;
}

bool
remove (const char *file)
{
  is_valid (file);
  if (*file ==0) { return false;}
  lock_acquire(&file_lock);
  bool success = filesys_remove (file);
  lock_release(&file_lock);
  return success;
}

int 
open (const char *file)
{
  is_valid(file);
  if(*file==0){ return -1; }
  struct file* openfile;
  lock_acquire(&file_lock);
  openfile = filesys_open(file);
  //file_deny_write(openfile);
  if (openfile == NULL)
  {
    lock_release(&file_lock);
    return -1;
  }
  else
  {
    //file_deny_write(openfile);
    int fd = thread_current()->fd_grant;
    thread_current()->fd_grant = fd + 1;

    /* need to add list of thread file struct to thread.h and add thread file struct with file, fd, elem */
    struct thread_file *thread_file;
    thread_file = malloc(sizeof(struct thread_file));
    thread_file->file = openfile;
    thread_file->fd = fd;
    list_push_back(&thread_current()->file_list, &thread_file->file_elem);
    lock_release(&file_lock);
    return fd;
  }
}

int
filesize (int fd)
{
  struct thread* curr = thread_current();
  struct list_elem * itr;
  struct list files = curr->file_list;
  struct thread_file * itrfile;

  for (itr=list_begin (&files); itr != list_end (&files); itr=list_next(itr))
  {
    itrfile = list_entry(itr, struct thread_file, file_elem);
    if (itrfile->fd != fd){ continue; }
    else
    {
      lock_acquire(&file_lock);
      int fl = file_length(itrfile->file);
      lock_release(&file_lock);
      return fl;
    }
  }
  return -1;
}

struct thread_file*
thread_file_by_fd(int fd)
{
  struct list_elem* i;
  struct list files;
  files = thread_current()->file_list;
  struct thread_file * thread_file;

  if (list_size(&files) == 0){ return NULL; }
  for(i=list_begin(&files); i!=list_end(&files);i=list_next(i))
  {
    if (i == list_tail(&files)){ break; }
    thread_file = list_entry(i, struct thread_file, file_elem);
    if (thread_file->fd == fd){ return thread_file; }
  }
  return NULL;
}

int
read (int fd, void *buffer, unsigned size)
{
  int actual_read;
  is_valid((char*)buffer + size -1);
  is_valid(buffer);
  lock_acquire(&file_lock);
  if (fd == 0)
  {
    /* std in*/
    int itr;
    for (itr = 0; itr < size; itr++)
    {
      *(uint8_t*)(buffer+itr) = input_getc ();
    }
    lock_release(&file_lock);
    return size;
  }
  else if (fd == 1)
  { 
    lock_release(&file_lock);
    return -1; 
  }
  else
  {
    /*struct thread* curr = thread_current();
    struct list_elem * itr;
    struct list files = curr->file_list;
    struct thread_file * itrfile;

    for (itr=list_begin(&files); itr!=list_end(&files); itr=list_next(itr))
    {
      if (list_next(itr) == list_tail(&files)){ break; }
      itrfile = list_entry(itr, struct thread_file, file_elem);
      if (itrfile->fd == fd){ return file_read (itrfile->file, buffer, size); }
    }
    return -1;*/

    struct file* file = file_by_fd (fd, thread_current());
    if (!file)
    {
      lock_release(&file_lock);
      return -1; 
    }
    else
    {
      actual_read = file_read(file, buffer, size); 
      lock_release(&file_lock);
      return actual_read;
    }
  }
}

int
write (int fd, const void * buffer, unsigned int size)
{
  is_valid((char*)buffer+size-1);
  is_valid(buffer);
  int actual_write;
  lock_acquire(&file_lock);
  if (fd == 1){
    lock_release(&file_lock);
    putbuf(buffer, size);
    return size;
  }
  else if (fd == 0)
  { 
    lock_release(&file_lock);
    return -1; 
  }
  else
  {
    if (size == 0) 
    {
      lock_release(&file_lock);
      return 0; 
    }
    //else { return size; }
    struct file * file = file_by_fd(fd, thread_current());
    if(!file)
    { 
      lock_release(&file_lock);
      return -1; 
    }
    else
    {
      if(file->isdir){ return -1; } /* not a file but directory */
      actual_write = file_write(file, buffer, size);
      lock_release(&file_lock);
      return actual_write;
    }
  }
}

void
seek (int fd, unsigned position)
{
  if (fd == 0 || fd == 1){ return ; }
  struct thread* curr= thread_current();
  struct list_elem * itr;
  struct list files = curr->file_list;
  struct thread_file * itrfile;

  for (itr=list_begin(&files);itr!=list_end(&files); itr=list_next(itr))
  {
    //printf("############### GOT IN ################# \n");
    itrfile = list_entry(itr, struct thread_file, file_elem);
    //printf("########### %d ##########", itrfile->fd);
    if (itrfile->fd != fd){ continue; }
    else
    {
      file_seek(itrfile->file, position);
      break;
    }
  }
}

unsigned
tell (int fd)
{
  if (fd == 0 || fd == 1 ){ return -1; }
  struct thread* curr=thread_current();
  struct list_elem * itr;
  struct list files = curr->file_list;
  struct thread_file* itrfile;

  for (itr=list_begin(&files);itr!=list_end(&files); itr=list_next(itr))
  {
    itrfile = list_entry(itr, struct thread_file, file_elem);
    if(itrfile->fd != fd){ continue; }
    else
    {
      unsigned next_byte = file_tell(itrfile->file);
      return next_byte;
    }
  }
  return -1;
}

void
close (int fd)
{
  lock_acquire(&file_lock);
  if (fd == 0 || fd == 1)
  { 
    lock_release(&file_lock);
    return ; 
  }

  struct thread* curr = thread_current();
  struct list_elem * itr;
  struct list_elem * mmap_itr;
  struct list *files = &curr->file_list;
  struct thread_file* itrfile;
  struct file* file;
  struct list* mmap_lst = &curr->mmap_lst;
  struct spte* spte;

  for(itr=list_begin(files);itr!=list_end(files); itr=list_next(itr))
  {
    itrfile = list_entry(itr, struct thread_file, file_elem);
    if(itrfile->fd != fd){ continue; }
    else{
      file = itrfile->file;
      for(mmap_itr=list_begin(mmap_lst); mmap_itr != list_end(mmap_lst); mmap_itr = list_next(mmap_itr))
      {
        spte = list_entry(mmap_itr, struct spte, mmap_elem);
        if(spte->file == file && spte->type == MMAP){ get_lazy_file(spte); }
      }

      if (itrfile->file->deny_write){ file_allow_write(itrfile->file); }
      list_remove(itr);
      file_close(file);
      free(itrfile);
      break;
    }
  }
  lock_release(&file_lock);
  return;
}


int 
mmap(int fd, void* addr)
{
  void* upage = addr;
  struct thread* curr = thread_current();
  struct file* file = file_by_fd(fd, curr);
  if(!file){ return -1; }
  int mapid = ++curr->mapid;
  
  size_t page_read_bytes;

  uint32_t read_bytes = file_length(file);
  if (read_bytes==0){ return -1; }

  off_t offset = 0;

  bool fd_check = fd == 1 || fd == 0;
  bool addr_check = addr == 0 || pg_ofs(addr) != 0;
  if (fd_check || addr_check){ return -1; }

  while (read_bytes > 0)
  {
    if (read_bytes < PGSIZE){ page_read_bytes = read_bytes; }
    else{ page_read_bytes = PGSIZE; }

    if (pagedir_get_page(curr->pagedir, upage))
    {
      munmap(mapid);
      return -1;
    }
    if (page_lookup(&curr->spt, upage))
    {
      munmap(mapid);
      return -1; 
    }

    struct spte* spte = get_spte(upage, MEMORY, true);
    spte->type = MMAP;
    spte->offset = offset;
    spte->size = page_read_bytes;
    spte->file = file;
    spte->mapid = mapid;

    list_push_back(&curr->mmap_lst, &spte->mmap_elem);

    read_bytes = read_bytes - page_read_bytes;
    upage = upage + PGSIZE;
    offset = offset + page_read_bytes;
  }
  return mapid;
}

void 
munmap(int mapid)
{
  struct list_elem* itr;
  struct thread* curr = thread_current();
  struct list* mmap_lst = &curr->mmap_lst;
  struct spte* spte;
  itr = list_begin(mmap_lst);
  while (itr != list_end(mmap_lst))
  {
    spte = list_entry(itr, struct spte, mmap_elem);
    itr = list_next(itr);
    if(spte->mapid == mapid)
    {
      if(spte->type == SWAP) { get_swap(spte); }
      if(spte->type == MEMORY)
      {
        if (pagedir_is_dirty(spte->thread->pagedir, spte->address))
        {
          lock_acquire(&file_lock);
          file_write_at(spte->file, spte->address, spte->size, spte->offset);
          lock_release(&file_lock);
        }
        f_free_spte(spte);
      }
      list_remove(&spte->mmap_elem);
      hash_delete(&spte->thread->spt, &spte->hash_elem);
      free(spte);
    }
  }
}

bool 
chdir (const char* path)
{
  return filesys_chdir (path);
}

bool
mkdir (const char * path)
{
  bool ret = filesys_create(path, 0, true);
  return ret;
}

bool
readdir (int fd, char* name)
{
  struct thread* curr = thread_current();
  struct file* f = file_by_fd(fd, curr);
  if(!f || !f->isdir) { return false; }
  
  struct dir* dir = dir_open(f->inode);
  dir_seek(dir, file_tell(f));

  bool ret = dir_readdir(dir, name);
  file_seek(f, dir_tell(dir));

  dir_close(dir);
  return ret;
}

bool
isdir (int fd)
{
  struct thread* curr = thread_current();
  struct file* f = file_by_fd (fd, curr);
  if (!f) { return false; }
  return f->isdir;
}

int
inumber (int fd)
{
  struct thread* curr = thread_current();
  struct file* f = file_by_fd (fd, curr);
  if(!f){ return false; }
  return inode_get_inumber(f->inode);
}







