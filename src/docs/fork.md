# Fork System Call Design Document

## Data Structures and Functions

### New Data Structures

```c
/* Structure for synchronizing fork operation between parent and child */
struct fork_info {
  struct intr_frame* parent_if;     /* Parent's interrupt frame to copy */
  struct semaphore fork_sema;       /* Synchronization semaphore */
  bool fork_success;                /* True if child setup succeeded */
  struct process* parent_pcb;       /* Parent's process control block */
  struct process* child_pcb;        /* Child's PCB (set by child thread) */
};
```

### Modified Data Structures

No existing data structures require modification. The existing `struct process` and `struct thread` are sufficient.

### New Functions

```c
/* System call handler for fork */
static pid_t sys_fork(struct intr_frame* f);

/* Thread function that sets up the child process */
static void fork_child_process(void* fork_info_);

/* Copy parent's file descriptor table to child */
static bool copy_file_descriptors(struct process* child_pcb, struct process* parent_pcb);

/* Copy parent's page directory to child (already exists as pagedir_copy) */
uint32_t* pagedir_copy(uint32_t* src);
```

### Modified Functions

```c
/* Add SYS_FORK case in syscall_handler() in syscall.c */
static void syscall_handler(struct intr_frame* f);
```

## Algorithms

### Fork System Call Implementation

The fork system call follows a copy-on-creation approach rather than the load-from-executable approach used by exec. This leverages the fact that the parent process is already running and has a complete address space.

**Parent Process Flow:**
1. Create a `fork_info` structure containing the current interrupt frame and synchronization primitives
2. Launch a new kernel thread running `fork_child_process()` with the fork_info
3. Block on `fork_sema` waiting for child setup completion
4 . Check `fork_success` flag and return either child PID or -1

**Child Process Setup:**
1. Allocate new process control block
2. Copy parent's page directory using `pagedir_copy()` 
3. Copy parent's file descriptor table (incrementing reference counts)
4. Initialize child-specific PCB fields (parent pointer, children list, etc.)
5. Signal parent via `fork_sema` with success/failure status
6. If successful, copy parent's interrupt frame with `eax = 0` and jump to user mode
7. If failed, clean up and exit thread

### Kernel Data Structure Creation

Fork creates entirely new kernel data structures for the child process:

**New Thread Structure:**
- `thread_create()` allocates new thread with unique TID
- New kernel stack and thread control block
- Child TID becomes the child's PID

**New Process Structure:**
- `malloc()` allocates new `struct process`
- Gets new PID (same as thread TID)
- Completely independent process control block

### Address Space Copying

The `pagedir_copy()` function handles copying all mapped pages from parent to child. This creates a complete duplicate of the parent's virtual address space, including:
- Code segments (executable instructions)
- Data segments (global/static variables) 
- BSS segments (uninitialized data)
- Heap (dynamically allocated memory)
- Stack (local variables, function calls)

**Implementation Details:**
1. Create new page directory with `pagedir_create()`
2. For each mapped virtual page in parent:
   - `palloc_get_page()` - allocate new physical page for child
   - `memcpy()` - copy contents from parent's physical page to child's
   - `pagedir_set_page()` - map child's virtual address to new physical page
3. Result: Child has identical virtual addresses pointing to separate physical memory

### Interrupt Frame Access and Copying

The parent's execution state is captured in the interrupt frame automatically:

**How to Access:**
- System call interrupt creates `struct intr_frame` containing all CPU registers
- Syscall handler receives this frame as parameter: `syscall_handler(struct intr_frame* f)`
- Pass frame directly to fork implementation: `sys_fork(f)`

**Interrupt Frame Contents:**
- All CPU registers (EAX, EBX, ECX, EDX, ESI, EDI, EBP, ESP)
- Instruction pointer (EIP) - where to resume execution
- Stack pointer (ESP) - current stack location
- CPU flags (EFLAGS) and segment registers

**Child Frame Setup:**
```c
struct intr_frame child_if = *(parent_if);  // Copy entire frame
child_if.eax = 0;  // Child returns 0 from fork()
// Jump to user mode: both processes resume at same EIP with different return values
```

### Return Value Handling

Both parent and child resume execution from the same instruction pointer but with different return values:
- **Parent:** Receives `child_pid` in `f->eax` from syscall handler before returning to user mode
- **Child:** Receives `0` in copied interrupt frame before jumping to user mode
- **Both:** Resume execution at same EIP (instruction after `fork()` system call)

### File Descriptor Copying

Child inherits copies of all parent file descriptors. The `copy_file_descriptors()` function:
1. Iterates through parent's `open_files` list
2. Creates new file_descriptor structures for child
3. References the same underlying `struct file` objects (shared between processes)
4. Maintains separate file positions (since `struct file` contains position state)

### Process Control Block Field Handling

**Fields Copied from Parent:**
```c
strlcpy(child_pcb->process_name, parent_pcb->process_name, 16);  // Same process name
child_pcb->next_fd = parent_pcb->next_fd;                       // Same next FD number
child_pcb->executable_file = parent_pcb->executable_file;       // Share same executable
```

**Fields Initialized Fresh:**
```c
list_init(&child_pcb->children);           // Empty children list (NOT inherited)
lock_init(&child_pcb->children_lock);      // New lock for children list
child_pcb->parent_pcb = parent_pcb;        // Point to original parent (not NULL)
child_pcb->exit_status = -1;               // Default exit status
child_pcb->main_thread = child_thread;     // Point to new child thread
child_pcb->pagedir = pagedir_copy(...);    // New page directory with copied contents
```

**Key Points:**
- **Children NOT inherited:** Child starts with empty children list, doesn't inherit parent's children
- **Parent relationship:** Child's parent points to original parent, maintaining process tree structure
- **New identities:** Child gets new PID/TID but inherits parent's name and file access patterns

## Synchronization

### Shared Resources

**File System Lock (`filesys_lock`):**
- **Access Pattern:** Acquired during file descriptor copying
- **Strategy:** Use existing file system locking to ensure atomic file operations
- **Rationale:** Prevents race conditions when multiple processes access file system simultaneously

**Parent's PCB and File Descriptor List:**
- **Access Pattern:** Read by child during setup while parent is blocked
- **Strategy:** Parent blocks on semaphore during child setup, ensuring parent's state doesn't change
- **Rationale:** Child needs stable view of parent's file descriptors during copying

**Child's PCB:**
- **Access Pattern:** Written by child thread, read by parent after semaphore signal
- **Strategy:** Parent only accesses child PCB after `fork_sema` is signaled
- **Rationale:** Ensures child PCB is fully initialized before parent can access it

**Fork Info Structure:**
- **Access Pattern:** Shared between parent and child thread
- **Strategy:** Parent allocates on stack, child only writes to success flag, synchronization via semaphore
- **Rationale:** Simple producer-consumer pattern with clear ownership

### Memory Management

**Page Directory Copying:**
- **Concurrency:** Each process gets independent page directory, no sharing concerns
- **Memory Cost:** Doubles memory usage temporarily during copy operation
- **Time Cost:** O(n) where n is number of mapped pages

### Synchronization Limitations

**Limited Concurrency:** Parent is blocked during entire child setup process (typically brief)
**Memory Pressure:** Fork temporarily doubles memory usage, could fail under memory pressure
**File Descriptor Limits:** Child inherits all file descriptors, potentially hitting system limits

## Rationale

### Design Advantages

**Simplicity:** Leverages existing process creation infrastructure while avoiding complex executable loading
**Correctness:** Clear synchronization pattern borrowed from proven `process_execute` implementation
**Performance:** Avoids redundant ELF parsing and loading since parent is already running
**Maintainability:** Minimal changes to existing codebase, clear separation of concerns

### Alternative Approaches Considered

**Option 1: Async Fork with Deferred Copying**
- **Rejected:** More complex synchronization, harder to determine when child is "ready"
- **Trade-off:** Better concurrency but increased complexity and error-prone edge cases

**Option 2: Copy-on-Write Implementation**
- **Rejected:** Requires substantial VM system changes, beyond scope of current project
- **Trade-off:** Memory efficiency but implementation complexity

**Option 3: Fork as Special Case of Exec**
- **Rejected:** Would require serializing process state to temporary storage
- **Trade-off:** Code reuse but performance overhead and complexity

### Design Shortcomings

**Memory Usage:** Doubles memory consumption during fork operation
**Blocking Parent:** Parent cannot continue until child setup completes
**Error Recovery:** Limited error recovery options if child setup partially succeeds

### Extension Considerations

The design supports future extensions:
- **Copy-on-Write:** Page directory copying can be enhanced without changing interfaces
- **File Descriptor Optimization:** Reference counting can be added to file descriptor copying
- **Performance Optimization:** Child setup can be parallelized with parent execution using more sophisticated synchronization

The current design prioritizes correctness and simplicity over performance optimization, making it suitable for initial implementation while allowing future enhancements.

## Implementation Summary

### What Gets Created New:
- **Thread structure:** New kernel thread with unique TID
- **Process structure:** New PCB with unique PID
- **Page directory:** New virtual-to-physical mappings
- **Physical memory pages:** Separate copies of all memory content
- **File descriptor structures:** New FD entries pointing to same files

### What Gets Copied:
- **Address space contents:** All memory pages (code, data, heap, stack)
- **CPU state:** Complete interrupt frame with all registers
- **File descriptor table:** Access to same files with same positions
- **Process attributes:** Name, next FD number, executable file reference

### What Gets Initialized Fresh:
- **Children list:** Empty (child doesn't inherit parent's children)
- **Parent pointer:** Points to original parent
- **Synchronization objects:** New locks and semaphores
- **Exit status:** Default value (-1)

### Key Insight:
Fork creates a **complete duplicate** of a running process. Both processes have:
- **Identical virtual memory layouts** pointing to **separate physical memory**
- **Identical CPU register state** except for **different return values**
- **Shared access to files** but **independent file descriptor tables**
- **Independent process identities** with **clear parent-child relationship**

This design enables both processes to resume execution from the same point while maintaining complete independence for future modifications.
