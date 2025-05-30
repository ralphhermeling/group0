#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "process.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "threads/vaddr.h"

static void syscall_handler(struct intr_frame*);

void syscall_init(void) { intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall"); }

static void syscall_handler(struct intr_frame* f UNUSED) {
  uint32_t* args = ((uint32_t*)f->esp);

  /*
   * The following print statement, if uncommented, will print out the syscall
   * number whenever a process enters a system call. You might find it useful
   * when debugging. It will cause tests to fail, however, so you should not
   * include it in your final submission.
   */

  /* printf("System call number: %d\n", args[0]); */

  if (args[0] == SYS_EXIT) {
    f->eax = args[1];
    printf("%s: exit(%d)\n", thread_current()->pcb->process_name, args[1]);
    process_exit();
  } else if (args[0] == SYS_WRITE) {
    int fd = args[1];
    const char* buffer = (const char*)args[2];
    unsigned size = args[3];

    if (!is_user_vaddr(args) || !is_user_vaddr(args + 3)) {
      process_exit();
    }

    if (!is_user_vaddr(buffer) || !is_user_vaddr(buffer + size - 1)) {
      process_exit();
    }

    /* Only support STDOUT for now */
    ASSERT(fd == 1);

    /* Write number of size character of buffer to STDOUT */
    putbuf(buffer, size);
    f->eax = size;
  }
}
