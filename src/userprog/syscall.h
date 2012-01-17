#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/thread.h"

void syscall_init (void);

void exit (int);

void release_files (struct thread* cur);

#endif /* userprog/syscall.h */
