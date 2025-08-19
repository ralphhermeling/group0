# File Operation Syscalls Design

## Data Structures and Functions

### New Data Structures

```c
/* userprog/process.h */
struct process {
  // ... existing fields
  struct list open_files;           // List of open file descriptors for this process
  int next_fd;                      // Next available file descriptor number
  struct file* executable_file;     // Pointer to process's executable file (for write protection)
};

/* userprog/syscall.h or userprog/process.h */
struct file_descriptor {
  int fd;                          // File descriptor number (unique within process)
  struct file* file;               // Pointer to open file from Pintos file system
  struct list_elem elem;           // List element for process's open_files list
};
```

### Constants

```c
/* userprog/syscall.c */
#define STDIN_FILENO 0               // Standard input file descriptor
#define STDOUT_FILENO 1              // Standard output file descriptor  
#define FIRST_FILE_FD 2              // First available fd for actual files
```

### Global Variables

```c
/* userprog/syscall.c */
static struct lock filesys_lock;    // Global lock protecting all file system operations
```

### New Functions

```c
/* userprog/syscall.c */
struct file_descriptor* find_file_descriptor(int fd);
// Find file_descriptor in current process's open_files list

int init_file_descriptor(struct process* pcb, struct file* file);
// Create new file descriptor, add to process's file table, return fd number

void destroy_file_descriptor(struct file_descriptor* fd_entry);
// Remove file descriptor from table and free memory

void close_all_files(struct process* pcb);
// Close all open files when process exits
```

## Algorithms

### Process Initialization and Cleanup

**In `start_process()` after successful executable load:**
- Initialize `next_fd` to `FIRST_FILE_FD`
- Open executable file using `filesys_open()`
- Store pointer in `executable_file` field  
- Call `file_deny_write()` on executable to prevent modification

**In `process_exit()`:**
- Call `file_allow_write()` and `file_close()` on `executable_file`
- Call `close_all_files()` to close all open file descriptors

### Individual Syscall Implementations

**`sys_create(file_name, initial_size)`:**
1. Validate `file_name` pointer and string
2. Acquire `filesys_lock`
3. Call `filesys_create(file_name, initial_size)`  
4. Release `filesys_lock`
5. Return success/failure

**`sys_remove(file_name)`:**
1. Validate `file_name` pointer and string
2. Acquire `filesys_lock`
3. Call `filesys_remove(file_name)`
4. Release `filesys_lock`  
5. Return success/failure

**`sys_open(file_name)`:**
1. Validate `file_name` pointer and string
2. Acquire `filesys_lock`
3. Call `filesys_open(file_name)` to get `struct file*`
4. If successful, call `init_file_descriptor()` to create fd entry
5. Release `filesys_lock`
6. Return file descriptor number or -1

**`sys_read(fd, buffer, size)`:**
1. Validate buffer pointer range
2. Handle special cases: if `fd == STDIN_FILENO`, use `input_getc()` in loop
3. Acquire `filesys_lock`
4. Find file descriptor using `find_file_descriptor(fd)`
5. If found, call `file_read(file, buffer, size)`
6. Release `filesys_lock`
7. Return bytes read or -1

**`sys_write(fd, buffer, size)`:**
1. Validate buffer pointer range  
2. Handle edge case: if `size <= 0`, return 0
3. Handle special cases:
   - If `fd == STDIN_FILENO`, return -1
   - If `fd == STDOUT_FILENO`, call `putbuf()` and return size
4. Acquire `filesys_lock`
5. Find file descriptor using `find_file_descriptor(fd)`
6. If found, call `file_write(file, buffer, size)`
7. Release `filesys_lock`
8. Return bytes written or -1

**`sys_seek(fd, position)` and `sys_tell(fd)`:**
1. Acquire `filesys_lock`
2. Find file descriptor using `find_file_descriptor(fd)`
3. If found, call `file_seek()` or `file_tell()`
4. Release `filesys_lock`
5. Return position or do nothing if fd invalid

**`sys_filesize(fd)` and `sys_close(fd)`:**
1. Acquire `filesys_lock`  
2. Find file descriptor using `find_file_descriptor(fd)`
3. If found, call `file_length()` or `file_close()` respectively
4. For close: remove from process's fd table and free fd structure
5. Release `filesys_lock`
6. Return size/-1 or success/failure

## Synchronization

### Shared Resources
- **Pintos file system**: Accessed by all processes performing file operations
- **Process file descriptor tables**: Each process's `open_files` list accessed by that process's threads

### Synchronization Strategy
- **Single global lock**: `filesys_lock` protects all file system operations
- **Coarse-grained locking**: All file syscalls acquire this lock for entire operation duration
- **Process-local data**: Each process's file descriptor table accessed only by that process (no additional locking needed)

### Rationale for Global Lock
- **Pintos file system is not thread-safe**: Requires exclusive access
- **Simple and correct**: Eliminates all race conditions in file operations  
- **Acceptable performance cost**: File I/O is inherently slow, lock contention minimal
- **Easy to verify**: No complex lock ordering or deadlock scenarios

### Concurrency Limitations
- **No concurrent file operations**: Only one thread system-wide can perform file I/O
- **Stdin/stdout bypass locking**: Console operations don't acquire file lock
- **Memory validation outside critical section**: Reduces time holding lock

## Rationale

### Design Advantages
1. **Correctness**: Global lock ensures thread safety with non-thread-safe Pintos file system
2. **Simplicity**: Single lock eliminates complex synchronization logic and deadlock scenarios  
3. **Standard Unix semantics**: File descriptors are process-local, support standard open/read/write/close operations
4. **Executable protection**: Automatic write denial using existing Pintos `file_deny_write()` mechanism
5. **Clean separation**: File descriptor management separate from underlying file system operations

### Alternative Approaches Considered
1. **Per-file locking**: More complex, requires global file tracking, prone to deadlocks
2. **Reader-writer locks**: Unnecessary complexity since Pintos file system isn't designed for it
3. **Lock-free approaches**: Not feasible with non-thread-safe underlying file system

### Potential Shortcomings
1. **Limited concurrency**: Only one file operation at a time system-wide
2. **Lock granularity**: Could potentially use finer-grained locking in future with thread-safe file system
3. **Memory overhead**: Each open file requires file descriptor structure

### Extension Considerations
- Design easily extensible for per-file locking when file system becomes thread-safe
- File descriptor table could be converted to hash table for better performance with many open files  
- Additional file modes (append, etc.) easily added to file descriptor structure
