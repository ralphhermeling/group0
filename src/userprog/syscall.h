#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "userprog/process.h"
#include "threads/synch.h"

extern struct lock filesys_lock; /* Global filesystem lock */

void syscall_init(void);
void syscall_exit(int status);
int sys_wait(pid_t pid);                                 /* System call handler for wait */
void destroy_file_descriptor_table(struct process* pcb); /* Destroys fdt properly */
#endif                                                   /* userprog/syscall.h */
