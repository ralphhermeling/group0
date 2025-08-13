#include "userprog/syscall.h"
#include <stdint.h>
#include <stdio.h>
#include <syscall-nr.h>
#include "devices/shutdown.h"
#include "process.h"
#include "string.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
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
    struct file_descriptor* fd_entry);     // Remove file descriptor from table and free memory
void close_all_files(struct process* pcb); // Close all open files when process exits
static struct lock filesys_lock;           // Global lock protecting all file system operations

void syscall_init(void) { intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall"); }
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
    default:
      printf("Unimplemented system call: %d\n", (int)args[0]);
      break;
  }

  t->in_syscall = false;
}
