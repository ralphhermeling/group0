#include "userprog/syscall.h"
#include <stdint.h>
#include <stdio.h>
#include <syscall-nr.h>
#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "list.h"
#include <stdlib.h>
#include "process.h"
#include "string.h"
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "userprog/process.h"
#include "threads/vaddr.h"
#include <stdbool.h>

#define MAX_ARGS 4
#define NUM_BYTES_ARGUMENT_STACK 4

static void syscall_handler(struct intr_frame*);
struct file_descriptor {
  int fd;                /* File descriptor number (unique within process) */
  struct file* file;     /* Pointer to open file from Pintos file system */
  struct list_elem elem; /* List element for process's open_files list */
};
struct file_descriptor*
find_file_descriptor(int fd); // Find file_descriptor in current process's open_files list
int init_file_descriptor(
    struct process* pcb,
    struct file* file); // Create new file descriptor, add to process's file table, return fd number
void destroy_file_descriptor(
    struct file_descriptor* fd_entry); // Remove file descriptor from table and free memory
struct lock filesys_lock;

struct file_descriptor* find_file_descriptor(int fd) {
  if (fd < FIRST_FILE_FD) {
    return NULL;
  }

  struct process* pcb = thread_current()->pcb;

  struct list_elem* e;
  for (e = list_begin(&pcb->open_files); e != list_end(&pcb->open_files); e = list_next(e)) {
    struct file_descriptor* file_descriptor = list_entry(e, struct file_descriptor, elem);
    if (file_descriptor != NULL && file_descriptor->fd == fd) {
      return file_descriptor;
    }
  }
  return NULL;
}

void destroy_file_descriptor_table(struct process* pcb) {
  lock_acquire(&filesys_lock);
  while (!list_empty(&pcb->open_files)) {
    struct list_elem* e = list_pop_back(&pcb->open_files);
    struct file_descriptor* file_descriptor = list_entry(e, struct file_descriptor, elem);
    file_close(file_descriptor->file);
    free(file_descriptor);
  }
  lock_release(&filesys_lock);
}

void syscall_init(void) {
  lock_init(&filesys_lock);
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}
void syscall_exit(int status) {
  printf("%s: exit(%d)\n", thread_current()->pcb->process_name, status);
  thread_current()->pcb->exit_status = status;
  process_exit();
}

static pid_t syscall_exec(char* cmd_line) { return process_execute(cmd_line); }

static int syscall_write(int fd, void* buffer, unsigned size) {
  if (size <= 0) {
    return 0;
  }
  if (fd == STDIN_FILENO) {
    return -1;
  }
  if (fd == STDOUT_FILENO) {
    /* Write number of size character of buffer to STDOUT */
    putbuf(buffer, size);
    return size;
  }
  struct file_descriptor* file_descriptor = find_file_descriptor(fd);
  if (file_descriptor == NULL) {
    return -1;
  }
  return file_write(file_descriptor->file, buffer, size);
}

static bool syscall_create(char* file, unsigned initial_size) {
  return filesys_create(file, initial_size);
}

static bool syscall_remove(char* file) {
  if (file == NULL) {
    return false;
  }
  return filesys_remove(file);
}

static int syscall_open(char* file) {
  if (file == NULL) {
    return -1;
  }

  struct file* f = filesys_open(file);
  if (f == NULL) {
    return -1;
  }

  struct file_descriptor* file_descriptor = malloc(sizeof(struct file_descriptor));
  if (file_descriptor == NULL) {
    return -1;
  }

  struct thread* t = thread_current();
  file_descriptor->fd = t->pcb->next_fd++;
  file_descriptor->file = f;

  list_push_back(&t->pcb->open_files, &file_descriptor->elem);
  return file_descriptor->fd;
}

static int syscall_filesize(int fd) {
  struct file_descriptor* file_descriptor = find_file_descriptor(fd);
  if (file_descriptor == NULL) {
    return -1;
  }

  return file_length(file_descriptor->file);
}

static bool syscall_close(int fd) {
  struct file_descriptor* file_descriptor = find_file_descriptor(fd);
  if (file_descriptor == NULL) {
    return false;
  }
  file_close(file_descriptor->file);
  list_remove(&file_descriptor->elem);
  free(file_descriptor);
  return true;
}

static int syscall_read(int fd, void* buffer, unsigned size) {
  if (fd == STDIN_FILENO) {
    uint8_t* byte_buffer = (uint8_t*)buffer;
    for (unsigned i = 0; i < size; i++) {
      byte_buffer[i] = input_getc();
    }
    return (int)size;
  }

  struct file_descriptor* file_descriptor = find_file_descriptor(fd);
  if (file_descriptor == NULL) {
    return -1;
  }

  return file_read(file_descriptor->file, buffer, size);
}

static int syscall_tell(int fd) {
  struct file_descriptor* file_descriptor = find_file_descriptor(fd);
  if (file_descriptor == NULL) {
    return -1;
  }
  return file_tell(file_descriptor->file);
}

static void syscall_seek(int fd, unsigned position) {
  struct file_descriptor* file_descriptor = find_file_descriptor(fd);
  if (file_descriptor == NULL) {
    syscall_exit(-1);
  }
  file_seek(file_descriptor->file, position);
}

static pid_t syscall_fork(struct intr_frame* f) { return process_fork(f); }

/*
 * This does not check that the buffer consists of only mapped pages; it merely
 * checks the buffer exists entirely below PHYS_BASE.
 */
static void validate_buffer_in_user_region(const void* buffer, size_t length) {
  uintptr_t delta = PHYS_BASE - buffer;
  if (!is_user_vaddr(buffer) || length > delta)
    syscall_exit(-1);
}

/*
 * This does not check that the string consists of only mapped pages; it merely
 * checks the string exists entirely below PHYS_BASE.
 */
static void validate_string_in_user_region(const char* string) {
  uintptr_t delta = PHYS_BASE - (const void*)string;
  if (!is_user_vaddr(string) || strnlen(string, delta) == delta)
    syscall_exit(-1);
}

/*
 * Safe validation functions that return true on success, false on failure
 * instead of calling syscall_exit directly. Use these when holding locks.
 */
static bool safe_validate_buffer_in_user_region(const void* buffer, size_t length) {
  uintptr_t delta = PHYS_BASE - buffer;
  return is_user_vaddr(buffer) && length <= delta;
}

static bool safe_validate_string_in_user_region(const char* string) {
  uintptr_t delta = PHYS_BASE - (const void*)string;
  return is_user_vaddr(string) && strnlen(string, delta) != delta;
}

static void syscall_handler(struct intr_frame* f) {
  uint32_t* args = f->esp;
  struct thread* t = thread_current();
  t->current_syscall = 0; /* Mark that we're in syscall handler but don't know which yet */
  validate_buffer_in_user_region(args, sizeof(uint32_t));
  /*
   * The following print statement, if uncommented, will print out the syscall
   * number whenever a process enters a system call. You might find it useful
   * when debugging. It will cause tests to fail, however, so you should not
   * include it in your final submission.
   */
  /* printf("System call number: %d\n", args[0]); */
  t->current_syscall = args[0];
  switch (args[0]) {
    case SYS_EXIT:
      validate_buffer_in_user_region(&args[1], sizeof(uint32_t));
      syscall_exit((int)args[1]);
      break;
    case SYS_WRITE:
      lock_acquire(&filesys_lock);
      validate_buffer_in_user_region(&args[1], 3 * sizeof(uint32_t));
      validate_buffer_in_user_region((void*)args[2], (unsigned)args[3]);
      f->eax = syscall_write((int)args[1], (void*)args[2], (unsigned)args[3]);
      lock_release(&filesys_lock);
      break;
    case SYS_PRACTICE:
      validate_buffer_in_user_region(&args[1], sizeof(uint32_t));
      f->eax = (int)args[1] + 1;
      break;
    case SYS_HALT:
      shutdown_power_off();
      break;
    case SYS_EXEC:
      validate_buffer_in_user_region(&args[1], sizeof(uint32_t));
      validate_string_in_user_region((char*)args[1]);
      f->eax = syscall_exec((char*)args[1]);
      break;
    case SYS_WAIT:
      validate_buffer_in_user_region(&args[1], sizeof(uint32_t));
      f->eax = process_wait((int)args[1]);
      break;
    case SYS_CREATE:
      lock_acquire(&filesys_lock);
      if (!safe_validate_buffer_in_user_region(&args[1], 2 * sizeof(uint32_t)) ||
          !safe_validate_string_in_user_region((char*)args[1])) {
        lock_release(&filesys_lock);
        syscall_exit(-1);
      }
      f->eax = syscall_create((char*)args[1], (unsigned)args[2]);
      lock_release(&filesys_lock);
      break;
    case SYS_REMOVE:
      lock_acquire(&filesys_lock);
      if (!safe_validate_buffer_in_user_region(&args[1], sizeof(uint32_t)) ||
          !safe_validate_string_in_user_region((char*)args[1])) {
        lock_release(&filesys_lock);
        syscall_exit(-1);
      }
      f->eax = syscall_remove((char*)args[1]);
      lock_release(&filesys_lock);
      break;
    case SYS_OPEN:
      lock_acquire(&filesys_lock);
      if (!safe_validate_buffer_in_user_region(&args[1], sizeof(uint32_t)) ||
          !safe_validate_string_in_user_region((char*)args[1])) {
        lock_release(&filesys_lock);
        syscall_exit(-1);
      }
      f->eax = syscall_open((char*)args[1]);
      lock_release(&filesys_lock);
      break;
    case SYS_FILESIZE:
      lock_acquire(&filesys_lock);
      if (!safe_validate_buffer_in_user_region(&args[1], sizeof(uint32_t))) {
        lock_release(&filesys_lock);
        syscall_exit(-1);
      }
      f->eax = syscall_filesize((int)args[1]);
      lock_release(&filesys_lock);
      break;
    case SYS_CLOSE:
      lock_acquire(&filesys_lock);
      if (!safe_validate_buffer_in_user_region(&args[1], sizeof(uint32_t))) {
        lock_release(&filesys_lock);
        syscall_exit(-1);
      }
      bool success = syscall_close((int)args[1]);
      lock_release(&filesys_lock);
      if (!success) {
        syscall_exit(-1);
      }
      break;
    case SYS_READ:
      lock_acquire(&filesys_lock);
      if (!safe_validate_buffer_in_user_region(&args[1], 3 * sizeof(uint32_t)) ||
          !safe_validate_buffer_in_user_region((void*)args[2], (unsigned)args[3])) {
        lock_release(&filesys_lock);
        syscall_exit(-1);
      }
      f->eax = syscall_read((int)args[1], (void*)args[2], (unsigned)args[3]);
      lock_release(&filesys_lock);
      break;
    case SYS_TELL:
      lock_acquire(&filesys_lock);
      if (!safe_validate_buffer_in_user_region(&args[1], sizeof(uint32_t))) {
        lock_release(&filesys_lock);
        syscall_exit(-1);
      }
      f->eax = syscall_tell((int)args[1]);
      lock_release(&filesys_lock);
      break;
    case SYS_SEEK:
      lock_acquire(&filesys_lock);
      if (!safe_validate_buffer_in_user_region(&args[1], 2 * sizeof(uint32_t))) {
        lock_release(&filesys_lock);
        syscall_exit(-1);
      }
      syscall_seek((int)args[1], (unsigned)args[2]);
      lock_release(&filesys_lock);
      break;
    case SYS_FORK:
      f->eax = syscall_fork(f);
      break;
    default:
      printf("Unimplemented system call: %d\n", (int)args[0]);
      break;
  }

  t->current_syscall = -1;
}
