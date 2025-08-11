# Simplified Wait Design

## Data Structures and Functions

```c
/* userprog/process.h */
struct process {
  /* ... existing fields ... */
  struct list children;           /* List of child_info for direct children */
  struct lock children_lock;      /* Protects children list */
  struct process* parent;          /* Parent thread, NULL if no parent */
};

/* New structure for tracking child processes */
struct child_info {
  pid_t pid;                     /* Child's process ID */
  int exit_status;              /* Child's exit status (-1 if killed by kernel) */
  bool has_exited;              /* True if child has called process_exit() */  
  bool has_been_waited;         /* True if parent has already waited for this child */
  struct semaphore exit_sema;   /* Signaled when child exits */
  struct list_elem elem;        /* List element for parent's children list */
};
```

```c
/* userprog/process.h */
struct child_info *create_child_info(pid_t pid);  /* Allocate and initialize child_info */
void destroy_child_info(struct child_info *info); /* Free child_info structure */
```

```c  
/* userprog/syscall.c */
int sys_wait(pid_t pid);  /* System call handler for wait */
```

## Algorithms

### Process Creation (`process_execute`)
1. After successfully creating child thread but before returning:
   - Allocate `struct child_info` with `create_child_info(child_tid)`
   - Initialize: `exit_status = -1`, `has_exited = false`, `has_been_waited = false`
   - Initialize semaphore: `sema_init(&exit_sema, 0)`
   - Acquire parent's `children_lock`
   - Add to parent's `children` list
   - Release `children_lock`
2. In child's `start_process()`, set `thread_current()->pcb->parent = parent_thread`

### Wait Implementation (`sys_wait`)
1. Get current thread as parent
2. Acquire `parent->pcb->children_lock`  
3. Search `parent->pcb->children` list for matching `pid`
   - If not found: release lock, return -1 (not a direct child)
   - If `has_been_waited` is true: release lock, return -1 (already waited)
4. Mark `has_been_waited = true`
5. If `has_exited` is false:
   - Release `children_lock` 
   - Call `sema_down(&child->exit_sema)` (blocks until child exits)
   - Reacquire `children_lock`
6. Read `exit_status` from `child_info->exit_status`
7. Release `children_lock`
8. Return the exit_status

### Process Exit (`process_exit`) 
1. If `thread_current()->pcb->parent` is not NULL:
   - Acquire `parent->children_lock`
   - Search parent's children list for entry with `pid == thread_current()->tid`  
   - If found: 
     - Set `child_info->exit_status = status` (where `status` comes from `sys_exit()` parameter, defaults to -1)
     - Set `child_info->has_exited = true`
     - Call `sema_up(&child_info->exit_sema)`
   - Release `parent->pcb->children_lock`
2. If `thread_current()->pcb->parent` is NULL:
   - Parent already exited, no need to signal anyone
3. Clean up own children list:
   - Acquire `children_lock`
   - For each child in children list: remove from list and `destroy_child_info()`
   - Release `children_lock`

### Parent Exit Before Child
In parent's `process_exit()`:
1. Acquire `children_lock`
2. For each `child_info` in children list:
   - If `child_info->has_exited == false`: 
     * Child is still alive, so `child_info->pid` corresponds to a valid thread
     * Find the child thread by TID and set `child_thread->pcb->parent = NULL`
   - If `child_info->has_exited == true`:
     * Child already exited, no need to update parent pointer
   - Remove `child_info` from list and `destroy_child_info()`
3. Release `children_lock`

## Synchronization

**Shared Resources:**
- `parent->pcb->children` list and `child_info` structures: accessed by parent (in wait) and child (in process_exit)
- `child_info->exit_sema`: signaled by child, waited on by parent

**Synchronization Strategy:**
- Each parent's `children_lock` protects that parent's children list and all associated `child_info` structures
- Lock is held only for short, bounded operations (list manipulation, flag checking/setting)
- Lock is **released before** blocking on `exit_sema` to prevent deadlock
- Child acquires parent lock only briefly to update status and signal semaphore

**Deadlock Prevention:**
- Never hold `children_lock` while blocking on semaphore
- Only one lock per parent (no lock ordering issues)
- Child never blocks while holding parent's lock

**Concurrency:**
- Different parent processes can wait independently (separate locks)
- Memory cost: O(number of children) per parent process
- Time cost: O(number of children) for list search, O(1) for synchronization

## Rationale

**Advantages:**
- **Simple and verifiable**: Single lock per parent, straightforward list-based tracking
- **Handles all requirements**: Direct child validation, double-wait prevention, exit status propagation
- **No memory leaks**: Child info cleaned up either in wait() or parent exit
- **Minimal code changes**: Leverages existing Pintos list and semaphore primitives

**Trade-offs:**
- **O(n) child lookup**: Linear search through children list (acceptable for typical use cases)
- **Parent death complexity**: Must handle parent exiting before child, but simplified compared to reference counting
- **Memory overhead**: One semaphore per child (but simpler than bidirectional reference counting)

**Compared to complex design:**
- Eliminates reference counting complexity while maintaining correctness
- Easier to implement and debug
- Still prevents zombies through cleanup in parent exit
- More straightforward to reason about synchronization

This design prioritizes simplicity and correctness over micro-optimizations, making it suitable for initial implementation and testing.

## TODO

Here's the plan:

1. Replace the current process_wait() with your actual wait algorithm
2. Remove temporary semaphore from process.c
3. Remove sema_up(&temporary) from process_exit()

The Key Insight

The kernel main thread becomes the "parent" of the initial process. So:

- When kernel calls process_execute("shell"), it should create a child_info for the shell process
- When kernel calls process_wait(shell_pid), it should use your wait algorithm
- When shell exits, it signals the kernel through the normal parent-child mechanism

Implementation Steps

1. In process_execute(): after creating child thread, create child_info and add to current thread's children list (kernel thread
 becomes parent)
2. Replace process_wait() with your wait algorithm
3. Remove all references to temporary semaphore
4. The kernel main thread will now properly wait for the initial process using your parent-child synchronization

This way, the same process_wait() function works for both:

- Kernel waiting for initial process
- User processes waiting for their children via sys_wait()
