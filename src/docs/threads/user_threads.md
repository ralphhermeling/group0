# User threads design doc

## Notes

More than one thread per user program --> the pid does not have to be equal to tid.
Implement pthread_create, pthread_exit and pthread_join (similar to wait) and
get_tid syscall.

Take a look at current project user programs syscall implementation to see if
they can handle multiple user threads per user process.

Maintain 1-1 mapping that is a user process with n user threads
should be paired 1-1 with n kernel threads, and each user thread should
run its dedicated kernel thread when it traps into the OS.

Implement user-level synchronization: lock_init, lock_acquire, sema_init,
sema_down, and sema_up for user programs.

## Out of scope

Multithreaded user programs will not call fork.

## User threads

We know's there's a 1-1 mapping.

```c
tid_t pthread_create(stub_fun sfun, pthread_fun tfun, const void* arg);
```

This function should create a new user thread and because we have a 1-1 mapping there also should be a new kernel thread allocated.
In the kernel thread struct of the main thread we can store the kernel threads allocated to user program.
For the thread id of the user thread we can use the kernel thread id since we have a 1-1 mapping.

The user thread has it's own:

- Stack
- CPU registers and program counter
- Thread Id (= same as kernel thread id)

Instead of loading an ELF and getting an entrypoint the user provided function is the entry point.
Arguments need to be passed onto the user thread's stack.

Must allocate new virtual pages for stack in existing virtual address space from pcb. Create fixed size stacks of 4MB.

When an user thread exits it shouldn't destroy the whole process like process_exit. It should deallocate the stack and destroy the mapped kernel thread.

### Interrupt frame
Same segment selectors and flags as process creation
eip: points to sfun (stub function in user space) not an ELF entry
esp: points to top of newly allocated thread's stack
Arguments (pthread, arg) pushed onto own thread's stack

add reference counting to PCB to track active threads

```c
void pthread_exit(tid_t tid);
```
Deallocate thread's user stack pages, decrement the PCB ref count
If last thread call process_exit for full cleanup ref_count == 0
If not last thread call thread_exit

```c
bool pthread_join(tid_t tid);
```
Handle synchronization via semaphores

### Implementation
sys_pthread_create -> pthread_execute(sfun, pthread_fun, arg)
pthread_execute -> thread_create(..., start_pthread, thread_info)
start_pthread
Share parent's PCB (increment ref count)
Allocate user stack pages
Set up interrupt frame with sfun entry point
Jumpt to user space via asm_restore context

#### pthread_execute
Starts a new thread running a user thread

Init semaphore sync such that we know whether pthread has loaded succesfully

Do thread_create with start_pthread

If thread_create failed return -1 
If load failed return -1

If all went well add 1 to ref_count 

#### start_pthread
Runs in kernel mode and sets up interrupt frame:

- if_.eip = sfun (points to pthread_stub in user space)
- Push tf and arg onto stack and stack align

Set pcb to parent's pcb

Jump to user mode -> CPU switches to user mode

pthread_stub(tf, arg) executes in user mode:

- calls tf(arg)
- When tf returns calls pthread_exit

pthread_exit system call cleans up pthread

## Data Structures

### struct thread (threads/thread.h)

No additional fields needed in struct thread. The existing `pcb` field already points to the shared process control block.

### struct process (userprog/process.h)

Add the following fields to track user threads:

```c
struct process {
  // ... existing fields ...
  
  /* User thread management */
  struct list user_threads;     /* List of user_thread_info structures */
  struct lock threads_lock;     /* Protects user_threads list */
  int thread_ref_count;         /* Number of active user threads */
};
```

### struct user_thread_info (userprog/process.h)

New structure to track individual user thread state:

```c
struct user_thread_info {
  tid_t tid;                   /* Thread ID (same for kernel and pthread) */
  void* exit_value;            /* Return value from pthread_fun */
  struct semaphore exit_sema;   /* Signaled when thread exits */
  bool has_exited;             /* Thread completion status */
  bool joined;                 /* Has someone already joined this thread? */
  tid_t joiner_tid;            /* Which thread is joining (for debugging) */
  struct list_elem elem;       /* For PCB's user_threads list */
};
```

### struct pthread_info (userprog/process.h)

Structure passed to start_pthread containing thread creation parameters:

```c
struct pthread_info {
  struct process* parent_pcb;   /* PCB to share with new thread */
  stub_fun sfun;               /* Stub function entry point */
  pthread_fun tf;              /* User thread function */
  void* arg;                   /* Argument to pthread function */
  tid_t assigned_tid;          /* Assigned thread ID (same as kernel TID) */
  
  /* Synchronization for thread creation */
  struct semaphore load_sema;   /* Signaled when thread setup complete */
  bool load_success;           /* Whether thread creation succeeded */
};
```
