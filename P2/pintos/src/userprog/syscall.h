#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);

/* : syscall headers */
void halt (void);
void exit (int status);
//pid_t exec (const char *cmd_line);
//int wait (pid_t pid);
//bool create (const char *file, unsigned );
int write (int fd, const void* buffer, unsigned int size);

#endif /* userprog/syscall.h */
