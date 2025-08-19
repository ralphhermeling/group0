# Design doc Exec

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
