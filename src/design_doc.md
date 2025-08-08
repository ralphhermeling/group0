# Design doc

Argument Passing
Data Structures and Functions

Algorithms

Synchronization

Rationale

----------
Process Control Syscalls
Data Structures and Functions

For accessing user memory:
- static bool validate_user_buffer(const void* buffer, size_t size).
This does not check that the buffer consists of only mapped pages;
it merely checks the buffer exists entirely below PHYS_BASE. 

- static void validate_string_in_user_region(const char* string)
This does not check that the string consists of only mapped pages; it merely
checks the string exists entirely below PHYS_BASE.

- add bool in_syscall property to struct thread 
Used to determine whether pagefault in kernel mode occurs during syscall.

- add void syscall_exit(int status) to userprog/syscall.h
Used to terminate current user program

Algorithms
For the validate_user_buffer function I would check the delta between PHYS_BASE and buffer.
Then I would check whether buffer is valid user address and whether length is larger than the delta.

For the validate_string_in_user_region I again would calculate the delta.
Then I would check whether buffer points to a valid user address
and whether string memory region exceeds PHYS_BASE

In src/userprog/exception I would check when a page fault occurs the following are truthy: 
- kernel mode
- fault address is a valid virtual user address
- in_syscall
Then I know the page fault is because of unmapped memory and then I exit the user program with status -1.

For syscall_exit I would use the printf function to print out the exit code and then call process_exit.

For syscall_exec:
Runs executable whose name is given and returns the new process's program id.
If the program can not load for any reason, return -1
Thus parent process can not return from a call to exec until it knows whether the child succesfully loaded its executable.

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   process id, or TID_ERROR if the thread cannot be created. */
pid_t process_execute(const char* file_name)
calls

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t thread_create(const char* name, int priority, thread_func* function, void* aux) {

This is called within process_execute
tid = thread_create(file_name, PRI_DEFAULT, start_process, fn_copy);
where start_process is:
/* A thread function that loads a user process and starts it
   running. */


Synchronization
The new bool in_syscall is defined in USERPROG and it is accessed in the kernel during a system call and when a pagefault is handled in exception.c.

Rationale

----------
File Operation Syscalls
Data Structures and Functions

Algorithms

Synchronization

Rationale

----------
Concept check
1.
2.
3.

Data Structures and Functions
Modified struct process (in process.h):
struct process {
  uint32_t* pagedir;          /* Page directory. */
  char process_name[16];      /* Name of the main thread */
  thread* main_thread; /* Pointer to main thread */ 

  /* Synchronization for process loading */
  struct semaphore load_sema;  /* Semaphore to wait for load completion */
  bool load_success;           /* Whether the executable loaded successfully */
};

Modified process_execute() signature:
pid_t process_execute(const char* file_name);
- Returns PID of child on successful load, -1 on failure
- Will now block until child reports load status

Algorithms

process_execute() modification:
1. Create child thread as before with thread_create()
2. If thread creation succeeds:
  - Initialize load_sema with value 0 in child's PCB
  - Wait on child's load_sema using sema_down()
  - Check child's load_success flag
  - Return child PID if successful, -1 if load failed
3. If thread creation fails, return -1 immediately
start_process() modification:
1. Perform existing load operations (load() call)
2. Set load_success in PCB based on load result
3. Signal parent using sema_up(&pcb->load_sema)
4. If load failed, exit thread; if successful, continue to user mode

synchronization points:                                                                  
- Parent blocks in process_execute() after thread_create()
- Child signals completion in start_process() after load() attempt Parent wakes up, checks result, returns appropriate value

Synchronization

Shared Resources:
- struct process fields (load_sema, load_success):
  - Accessed by parent thread (process_execute) and child thread (start_process)
  - Protected by semaphore semantics - parent waits, child signals
  - No race condition: parent waits before child can signal
Synchronization Strategy:
- Binary semaphore pattern: load_sema initialized to 0
  - Parent calls sema_down() → blocks until child signals
  - Child calls sema_up() → wakes parent exactly once
  - No deadlock: child always signals regardless of load success/failure
  - No race condition: semaphore ensures proper ordering

Critical Sections:
- Child's PCB setup and signaling is atomic within start_process()
- Parent's wait and result check is atomic within process_execute()
- Maximum one thread blocked per child process (the parent)

Concurrency Impact:
- Minimal blocking: Only parent of loading process blocks
- No global locks: Each parent-child pair has independent synchronization
- Fast operation: Synchronization overhead is one semaphore operation
- Scalable: Multiple exec() calls can proceed independently

Rationale

Advantages of this design:

1. Simplicity: Uses standard semaphore pattern (producer-consumer)
2. Correctness: Eliminates race condition between thread creation and load completion
3. Minimal overhead: Only two semaphore operations per exec() call
4. Scalable: No global synchronization bottlenecks
5. Clean semantics: exec() returns exactly when load status is known

Alternatives considered:

1. Global semaphore with TID tracking: More complex, requires global state management
2. Condition variable: Overkill for binary signal, requires additional mutex
3. Busy waiting: Wasteful of CPU resources
4. Message passing: Over-engineered for simple parent-child communication

Potential shortcomings:
- Slight memory overhead (one semaphore + bool per process)
- Parent remains blocked during child's entire load process
- Child process cleanup on load failure still required

Design strengths:
- Easy to verify: Clear happens-before relationship via semaphore
- Minimal code changes: Reuses existing PCB and synchronization primitives
- Extensible: Pattern can be reused for other parent-child synchronization needs
- Robust: Handles both success and failure cases uniformly

