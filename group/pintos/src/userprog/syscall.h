#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);
int sys_write(int fd, void *buf,  unsigned size);
void sys_exit(int status);

#endif /* userprog/syscall.h */
