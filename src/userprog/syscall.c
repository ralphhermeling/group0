#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "process.h"
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

static void syscall_handler(struct intr_frame* f UNUSED) {
  uint32_t args[MAX_ARGS];

  if (!get_user_args(f->esp, args, 1)) {
    process_exit();
  }

  /*
   * The following print statement, if uncommented, will print out the syscall
   * number whenever a process enters a system call. You might find it useful
   * when debugging. It will cause tests to fail, however, so you should not
   * include it in your final submission.
   */

  /* printf("System call number: %d\n", args[0]); */

  if (args[0] == SYS_EXIT) {
    if (!get_user_args(f->esp + NUM_BYTES_ARGUMENT_STACK, &args[1], 1)) {
      printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
    }

    f->eax = args[1];
    printf("%s: exit(%d)\n", thread_current()->pcb->process_name, args[1]);
    process_exit();
  } else if (args[0] == SYS_WRITE) {
    if (!get_user_args(f->esp + NUM_BYTES_ARGUMENT_STACK, &args[1], 3)) {
      printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
      f->eax = -1;
      process_exit();
    }
    int fd = args[1];

    const char* buffer = (const char*)args[2];
    unsigned size = args[3];

    /* Only support STDOUT for now */
    ASSERT(fd == 1);

    /* Write number of size character of buffer to STDOUT */
    putbuf(buffer, size);
    f->eax = size;
    return;
  } else if (args[0] == SYS_PRACTICE) {
    if (!get_user_args(f->esp + NUM_BYTES_ARGUMENT_STACK, &args[1], 1)) {
      printf("%s: exit(%d)\n", thread_current()->pcb->process_name, -1);
      f->eax = -1;
      process_exit();
    }
    int i = args[1];

    f->eax = i + 1;
    return;
  }
}
