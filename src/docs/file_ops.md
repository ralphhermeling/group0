# File operation syscalls design

While a user process is running, you must ensure that nobody can modify its executable on disk.
The functions file_deny_write and file_allow_write can assist with this feature.

## Data Structures and Functions

```c
/* userprog/syscall.c */
static struct lock filesys_lock; // One global lock to protect all file system operations

/* userprog/process.h */
struct process {
  // ... other fields
  struct list open_files; // Open file descriptors
  int next_fd; // Next available file descriptor number
  struct file* executable_file; // Stores pointer to process' executable file
};

/* file/file.h */
struct file_descriptor {
  int fd;
  struct file* file; // Pointer to open file
  struct list_elem elem; // For process' open_files list
};

/* userprog/syscall.c */
struct file_descriptor* find_file_descriptor(int fd);
// Find file_descriptor in parents pcb
int init_file_descriptor(struct process* pcb, struct file* file);
// Init file descriptor for file and add to file descriptor table
int destroy_file_descriptor(int fd);
int destroy_file_description_table();
```

## Algorithms

For all syscalls it applies to validate user buffer memory region and extra validation for strings

In the start_process function where pcb block for the new process is allocated do:

- Init next_fd to NEXT_FILE_FD
- Set executable_file
- Call file_deny_write to process' executable_file

In the process_exit function do:
- Call file_allow_write to process' executable_file
- Close all open file descriptors
  - If the operation is unsuccessful, exit with -1

For syscall_create:
1. call underlying filesys create function

For syscall_remove:
1. call underlying filesys remove function

For syscall_open:
Since next_fd is initialized when pcb is initialized we can use it to initialize new file descriptor table entry
1. call underlying filesys open function
2. if succesfull create new file descriptor table entry


For syscall_filesize:
1. Find entry in file descriptor table
2. If not found exit with -1
3. Otherwise call underlying filesys filesize function

For syscall_read:
1. Find entry in file descriptor table
2. If not found exit with -1
3. If fd STDIN_FILENO use inputgetc
4. Otherwise call underlying filesys read function

For syscall_seek do nothing if fd does not correspond to an entry in the file descriptor table.

For syscall_write:
1. Validate user pointer for buffer
    - If buffer is invalid user pointer, return -1
    - If buffer + size - 1 is invalid user pointer, return -1
2. Handle edge cases
   - If size <= 0, return 0
3. Handle special file descriptors
   - If fd == STDIN_FILENO (0), return -1 (can't write to stdin)
   - If fd == STDOUT_FILENO (1):
     * Call putbuf(buffer, size) to write to console
     * Return size
4. Handle regular file descriptors
   - Acquire filesys_lock
   - Find file_descriptor in process's open_files list using fd
   - If fd not found in file descriptor table:
     * Release filesys_lock
     * Return -1
5. Write to file
   - Call file_write(file_descriptor->file, buffer, size)

For syscall_tell:
1. Find entry in file descriptor table
2. If not found exit with -1
3. Call underlying filesys tell function

For syscall_close:
1. Find entry in file descriptor table
2. If not found exit with -1
3. Call underlying filesys close function
4. Destroy entry in file descriptor table
