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
static struct lock filesys_lock;       // Global lock protecting all file system operations
//

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

static unsigned syscall_write(int fd, void* buffer, unsigned size) {
  /* Only support STDOUT for now */
  ASSERT(fd == 1);

  /* Write number of size character of buffer to STDOUT */
  putbuf(buffer, size);
  return size;
}

static bool syscall_create(char* file, unsigned initial_size) {
  lock_acquire(&filesys_lock);
  bool success = filesys_create(file, initial_size);
  lock_release(&filesys_lock);
  return success;
}

static bool syscall_remove(char* file) {
  if (file == NULL) {
    return false;
  }
  lock_acquire(&filesys_lock);
  bool success = filesys_remove(file);
  lock_release(&filesys_lock);
  return success;
}

static int syscall_open(char* file) {
  if (file == NULL) {
    return -1;
  }

  lock_acquire(&filesys_lock);
  struct file* f = filesys_open(file);
  if (f == NULL) {
    lock_release(&filesys_lock);
    return -1;
  }

  struct file_descriptor* file_descriptor = malloc(sizeof(struct file_descriptor));
  if (file_descriptor == NULL) {
    lock_release(&filesys_lock);
    return -1;
  }

  struct thread* t = thread_current();
  file_descriptor->fd = t->pcb->next_fd++;
  file_descriptor->file = f;

  list_push_back(&t->pcb->open_files, &file_descriptor->elem);
  lock_release(&filesys_lock);
  return file_descriptor->fd;
}

static int syscall_filesize(int fd) {
  lock_acquire(&filesys_lock);
  struct file_descriptor* file_descriptor = find_file_descriptor(fd);
  if (file_descriptor == NULL) {
    lock_release(&filesys_lock);
    return -1;
  }

  int file_size = file_length(file_descriptor->file);
  lock_release(&filesys_lock);
  return file_size;
}

static void syscall_close(int fd) {
  lock_acquire(&filesys_lock);
  struct file_descriptor* file_descriptor = find_file_descriptor(fd);
  if (file_descriptor == NULL) {
    lock_release(&filesys_lock);
    syscall_exit(-1);
  }
  list_remove(&file_descriptor->elem);
  free(file_descriptor);
  lock_release(&filesys_lock);
}

static int syscall_read(int fd, void* buffer, unsigned size) {
  lock_acquire(&filesys_lock);
  if (fd == STDIN_FILENO) {
    uint8_t* byte_buffer = (uint8_t*)buffer;
    for (unsigned i = 0; i < size; i++) {
      byte_buffer[i] = input_getc();
    }
    lock_release(&filesys_lock);
    return (int)size;
  }

  struct file_descriptor* file_descriptor = find_file_descriptor(fd);
  if (file_descriptor == NULL) {
    lock_release(&filesys_lock);
    return -1;
  }

  int bytes_read = file_read(file_descriptor->file, buffer, size);
  lock_release(&filesys_lock);
  return bytes_read;
}

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

static void syscall_handler(struct intr_frame* f) {
  uint32_t* args = f->esp;
  struct thread* t = thread_current();
  t->in_syscall = true;

  /*
   * The following print statement, if uncommented, will print out the syscall
   * number whenever a process enters a system call. You might find it useful
   * when debugging. It will cause tests to fail, however, so you should not
   * include it in your final submission.
   */
  /* printf("System call number: %d\n", args[0]); */
  validate_buffer_in_user_region(args, sizeof(uint32_t));
  switch (args[0]) {
    case SYS_EXIT:
      validate_buffer_in_user_region(&args[1], sizeof(uint32_t));
      syscall_exit((int)args[1]);
      break;
    case SYS_WRITE:
      validate_buffer_in_user_region(&args[1], 3 * sizeof(uint32_t));
      validate_buffer_in_user_region((void*)args[2], (unsigned)args[3]);
      f->eax = syscall_write((int)args[1], (void*)args[2], (unsigned)args[3]);
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
      validate_buffer_in_user_region(&args[1], 2 * sizeof(uint32_t));
      validate_string_in_user_region((char*)args[1]);
      f->eax = syscall_create((char*)args[1], (unsigned)args[2]);
      break;
    case SYS_REMOVE:
      validate_buffer_in_user_region(&args[1], sizeof(uint32_t));
      validate_string_in_user_region((char*)args[1]);
      f->eax = syscall_remove((char*)args[1]);
      break;
    case SYS_OPEN:
      validate_buffer_in_user_region(&args[1], sizeof(uint32_t));
      validate_string_in_user_region((char*)args[1]);
      f->eax = syscall_open((char*)args[1]);
      break;
    case SYS_FILESIZE:
      validate_buffer_in_user_region(&args[1], sizeof(uint32_t));
      f->eax = syscall_filesize((int)args[1]);
      break;
    case SYS_CLOSE:
      validate_buffer_in_user_region(&args[1], sizeof(uint32_t));
      syscall_close((int)args[1]);
      break;
    case SYS_READ:
      validate_buffer_in_user_region(&args[1], 3 * sizeof(uint32_t));
      validate_buffer_in_user_region((void*)args[2], (unsigned)args[3]);
      f->eax = syscall_read((int)args[1], (void*)args[2], (unsigned)args[3]);
      break;
    default:
      printf("Unimplemented system call: %d\n", (int)args[0]);
      break;
  }

  t->in_syscall = false;
}
