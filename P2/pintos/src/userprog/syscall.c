#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
  //printf ("system call!\n");

  int args[3];                 /* For arguments - maximum three for syscalls */

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
    /* system call without any argument */
    case SYS_HALT:
      halt();
      break;
    case SYS_EXIT:
      exit(args[0]);
      break;
    case SYS_WRITE:
      f->eax = write(args[0], (const void*)args[1], (unsigned int)args[2]);
      break;
  }

  //thread_exit ();
}

void
halt (void)
{
  power_off();
}

void
exit (int status)
{
  //printf("%s: exit(%d)\n", thread_current()->name, status);
  thread_current()->return_status = status;
  thread_exit ();
}

int
write (int fd, const void * buffer, unsigned int size)
{
  //printf("WRITE\n");
  if (fd == 1){ 
    putbuf(buffer, size);
    return size;
  }
  else if (fd == 0){ return -1; }
  else
  {
    return 1;
  }
}


