# Design doc

Argument Passing
Data Structures and Functions

Algorithms

Synchronization

Rationale

----------
Process Control Syscalls
Data Structures and Functions

For accessing user memory:
- static bool validate_user_buffer(const void* buffer, size_t size).
This does not check that the buffer consists of only mapped pages;
it merely checks the buffer exists entirely below PHYS_BASE. 

- static void validate_string_in_user_region(const char* string)
This does not check that the string consists of only mapped pages; it merely
checks the string exists entirely below PHYS_BASE.

- add bool in_syscall property to struct thread 
Used to determine whether pagefault in kernel mode occurs during syscall.

- add void syscall_exit(int status) to userprog/syscall.h
Used to terminate current user program

Algorithms
For the validate_user_buffer function I would check the delta between PHYS_BASE and buffer.
Then I would check whether buffer is valid user address and whether length is larger than the delta.

For the validate_string_in_user_region I again would calculate the delta.
Then I would check whether buffer points to a valid user address
and whether string memory region exceeds PHYS_BASE

In src/userprog/exception I would check when a page fault occurs the following are truthy: 
- kernel mode
- fault address is a valid virtual user address
- in_syscall
Then I know the page fault is because of unmapped memory and then I exit the user program with status -1.

For syscall_exit I would use the printf function to print out the exit code and then call process_exit.

Synchronization
The new bool in_syscall is defined in USERPROG and it is accessed in the kernel during a system call and when a pagefault is handled in exception.c.

Rationale

----------
File Operation Syscalls
Data Structures and Functions

Algorithms

Synchronization

Rationale

----------
Concept check
1.
2.
3.

