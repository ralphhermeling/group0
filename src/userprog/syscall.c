#include "userprog/syscall.h"
#include <stdint.h>
#include <stdio.h>
#include <syscall-nr.h>
#include "process.h"
#include "string.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"
#include <stdbool.h>

#define MAX_ARGS 4
#define NUM_BYTES_ARGUMENT_STACK 4

static void syscall_handler(struct intr_frame*);

void syscall_init(void) { intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall"); }
void syscall_exit(int status) {
  printf("%s: exit(%d)\n", thread_current()->pcb->process_name, status);
  process_exit();
}

static unsigned syscall_write(int fd, void* buffer, unsigned size) {
  /* Only support STDOUT for now */
  ASSERT(fd == 1);

  /* Write number of size character of buffer to STDOUT */
  putbuf(buffer, size);
  return size;
}

static bool validate_user_buffer(const void* buffer, size_t size) {
  uint8_t* start = (uint8_t*)buffer;
  uint8_t* end = start + size;

  for (uint8_t* ptr = start; ptr < end; ptr = pg_round_down(ptr) + PGSIZE) {
    if (!is_user_vaddr(ptr) || pagedir_get_page(thread_current()->pcb->pagedir, ptr) == NULL) {
      return false;
    }
  }

  return true;
}

static bool get_user_args(void* esp, uint32_t* dest, size_t argc) {
  if (!validate_user_buffer(esp, argc * sizeof(uint32_t)))
    return false;

  uint32_t* uargs = (uint32_t*)esp;
  for (size_t i = 0; i < argc; i++) {
    dest[i] = uargs[i];
  }
  return true;
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
UNUSED static void validate_string_in_user_region(const char* string) {
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
    default:
      printf("Unimplemented system call: %d\n", (int)args[0]);
      break;
  }

  t->in_syscall = false;
}
