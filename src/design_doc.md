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
                                                                                               
  Modified approach - use a shared synchronization structure:
  /* Structure to pass synchronization info to child */
  struct process_load_info {  
    char* file_name;              /* Command line to execute */                                
    struct semaphore* load_sema;  /* Semaphore for parent to wait on */
    bool* load_success;           /* Shared success flag */                                    
  };                                       
                                                                                               
  Modified process_execute():
  pid_t process_execute(const char* file_name) {
    /* Create synchronization objects on parent's stack */                 
    struct semaphore load_sema;                
    bool load_success = false;      
    struct process_load_info load_info;                                                        

    /* Initialize sync objects */  
    sema_init(&load_sema, 0);       
    load_info.file_name = copy_of_file_name;
    load_info.load_sema = &load_sema;                                                          
    load_info.load_success = &load_success;                                                    
                                                                                               
    /* Create child thread, pass load_info */
    tid_t tid = thread_create(file_name, PRI_DEFAULT, start_process, &load_info);
                                                                                               
    if (tid != TID_ERROR) {                                                                    
      /* Wait for child to signal load completion */                                           
      sema_down(&load_sema);                                                                   

      /* Check result */                                                                                                                                                                      
      return load_success ? tid : -1;                                                          
    }                                                                                                                                                                                         
    return -1;                                                                                 
  }
Modified start_process():                                                                                                                                                                   
  static void start_process(void* load_info_) {                                                                                                                                               
    struct process_load_info* info = (struct process_load_info*)load_info_;
                                               
    /* ... existing load code ... */
    bool success = load(file_name, &if_.eip, &if_.esp);

    /* Signal parent with result */
    *(info->load_success) = success;
    sema_up(info->load_sema);

    /* Continue or exit based on success */
    if (!success) {
      thread_exit();
    }
    /* ... continue to user mode ... */
  }

  Algorithms

  1. Parent (process_execute):
    - Create semaphore and success flag on stack
    - Package into load_info structure
    - Pass load_info to child via thread_create()
    - Wait on semaphore until child signals
    - Read success flag and return appropriate value
  2. Child (start_process):
    - Receive load_info from parent
    - Attempt to load executable
    - Write result to shared success flag
    - Signal parent via semaphore
    - Exit if load failed, continue if successful

  Synchronization

  Shared Resources:
  - load_sema: Owned by parent, accessed by child for signaling
  - load_success: Owned by parent, written by child, read by parent
  - load_info structure: Lives on parent's stack, shared with child

  Safety guarantees:
  - Parent creates all sync objects before child thread starts
  - Child accesses sync objects through pointer, no race on initialization
  - Semaphore ensures proper ordering: child writes success flag before parent reads it
  - Parent stack remains valid until child signals (parent is blocked waiting)

  Key advantage: No need to access child's PCB from parent - all synchronization data is controlled by parent and shared via pointer
