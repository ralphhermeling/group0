#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/synch.h"
#include "threads/thread.h"
#include <stdint.h>

// At most 8MB can be allocated to the stack
// These defines will be used in Project 2: Multithreading
#define MAX_STACK_PAGES (1 << 11)
#define MAX_THREADS 127

/* PIDs and TIDs are the same type. PID should be
   the TID of the main thread of the process */
typedef tid_t pid_t;

/* Thread functions (Project 2: Multithreading) */
typedef void (*pthread_fun)(void*);
typedef void (*stub_fun)(pthread_fun, void*);

/* The process control block for a given process. Since
   there can be multiple threads per process, we need a separate
   PCB from the TCB. All TCBs in a process will have a pointer
   to the PCB, and the PCB will have a pointer to the main thread
   of the process, which is `special`. */
struct process {
  /* Owned by process.c. */
  uint32_t* pagedir;          /* Page directory. */
  char process_name[16];      /* Name of the main thread */
  struct thread* main_thread; /* Pointer to main thread */
  struct list children;       /* List of child_info for direct children */
  struct lock children_lock;  /* Protects children list */
  struct process* parent_pcb; /* Parent thread, NULL if no parent */
};

/* New structure for tracking child processes */
struct child_info {
  pid_t pid;                  /* Child's process ID */
  int exit_status;            /* Child's exit status (-1 if killed by kernel) */
  bool has_exited;            /* True if child has called process_exit() */
  bool has_been_waited;       /* True if parent has already waited for this child */
  struct semaphore exit_sema; /* Signaled when child exits */
  struct list_elem elem;      /* List element for parent's children list */
  struct process* pcb;        /* Direct pointer to child's process structure */
};

void userprog_init(void);

pid_t process_execute(const char* file_name);
int process_wait(pid_t);
void process_exit(void);
void process_activate(void);

bool is_main_thread(struct thread*, struct process*);
pid_t get_pid(struct process*);

tid_t pthread_execute(stub_fun, pthread_fun, void*);
tid_t pthread_join(tid_t);
void pthread_exit(void);
void pthread_exit_main(void);

struct child_info* create_child_info(pid_t pid);  /* Allocate and initialize child_info */
void destroy_child_info(struct child_info* info); /* Free child_info structure */
#endif                                            /* userprog/process.h */
