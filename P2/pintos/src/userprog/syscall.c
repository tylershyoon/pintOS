#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
/* for filesys use */
#include "filesys/filesys.h"
#include "filesys/file.h"
/* for accessing user memory */
#include "threads/vaddr.h"

static void syscall_handler (struct intr_frame *);

/* syscall regarding files */
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned size);
void seek (int fd, unsigned position);
unsigned tell (int fd);

/* syscall regarding pid */
int exec (const char * cmd_line);
int wait (int pid);

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
  if (!is_user_valid(vaddr)){ exit(-1); }
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

  int args[3];     /* For arguments - maximum three for syscalls */

  //if(!is_user_vaddr(f->esp)){ exit(-1); }
  if(!is_user_vaddr(f->esp + 4) || !is_user_vaddr(f->esp + 8) || ! is_user_vaddr(f->esp + 12)){ exit(-1); }
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
      args[0] = *(int *)(4 + f->esp);
      break;
    /* two-argument syscalls */
    case SYS_CREATE:
    case SYS_SEEK:
      args[0] = *(int *)(4 + f->esp);
      args[1] = *(int *)(8 + f->esp);
      break;
    /* three-argument syscalls */
    case SYS_READ:
    case SYS_WRITE:
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
      if (args[0] == NULL){ exit(-1); }
      else{
        f->eax = create((const char*)args[0], (unsigned)args[1]);
      }
      break;
    case SYS_REMOVE:
      f->eax = remove((const char*)args[0]);
      break;
    case SYS_OPEN:
      if (args[0] == NULL){ exit(-1); }
      else{
        f->eax = open((const char*)args[0]);
      }
      break;
    case SYS_FILESIZE:
      f->eax = filesize((int)args[0]);
      break;
    case SYS_READ:
      f->eax = read((int)args[0],(const void *)args[1],(unsigned)args[2]);
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

  struct thread * curr = thread_current();
  curr->exit_status = status;
  //thread_current()->exit_status = status;
  struct list_elem* itr;

  while(!list_empty(&curr->file_list))
  {
    itr = list_pop_front(&curr->file_list);
    close(list_entry(itr, struct thread_file, file_elem)->fd);
  }

  thread_exit ();
}

int
exec (const char *cmd_line)
{
  if (!cmd_line){ return -1; }
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
  if(!file){ exit(-1); return -1; }
  lock_acquire(&file_lock);
  bool success = filesys_create (file, (off_t)initial_size);
  lock_release(&file_lock);
  return success;
}

bool
remove (const char *file)
{
  if (!file){ return false; }
  if(!is_user_vaddr(file)){ exit(-1); }
  lock_acquire(&file_lock);
  bool success = filesys_remove (file);
  lock_release(&file_lock);
  return success;
}

int 
open (const char *file)
{
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
  if (!is_user_vaddr(buffer)){ exit(-1); }
  if (fd == 0)
  {
    /* std in*/
    int itr;
    for (itr = 0; itr < size; itr++)
    {
      *(uint8_t*)(buffer+itr) = input_getc ();
    }
    return size;
  }
  else if (fd == 1){ return -1; }
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
    if (!file){ return -1; }
    else{ return file_read(file, buffer, size); }
  }
}

int
write (int fd, const void * buffer, unsigned int size)
{
  int written;
  //printf("WRITE\n");
  if (!is_user_vaddr(buffer))
  {
    exit(-1);
    return -1;
  }
  if (fd == 1){ 
    putbuf(buffer, size);
    return size;
  }
  else if (fd == 0){ return -1; }
  else
  {
    if (size == 0) { return 0; }
    //else { return size; }
    struct file * file = file_by_fd(fd, thread_current());
    if(!file){ return -1; }
    else{ return file_write(file, buffer, size); }
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
  if (fd == 0 || fd == 1){ return ; }

  struct thread* curr = thread_current();
  struct list_elem * itr;
  struct list *files = &curr->file_list;
  struct thread_file* itrfile;

  for(itr=list_begin(files);itr!=list_end(files); itr=list_next(itr))
  {
    itrfile = list_entry(itr, struct thread_file, file_elem);
    if(itrfile->fd != fd){ continue; }
    else{
      file_close(itrfile->file);
      list_remove(itr);
      free(itrfile);
      break;
    }
  }
}

