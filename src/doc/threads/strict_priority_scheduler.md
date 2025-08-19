# Strict Priority Scheduler Design Document

## Data Structures and Functions

### Modified Structures

```c
// threads/thread.h
struct thread {
    // Existing fields...
    int priority;                     // Base priority set by thread_set_priority
    struct list donations;            // List of donations sorted by priority (highest first)
    struct thread *donating_to;       // Thread this thread is donating to (for nested donation)
    struct list held_locks;           // List of locks this thread currently holds
};

// Donation structure to track priority donations
struct donation {
    int donated_priority;             // Priority value being donated
    struct thread *donor;             // Thread making the donation
    struct lock *lock;                // Lock associated with this donation
    struct list_elem elem;            // List element for donations list
};

// threads/synch.h
struct lock {
    struct thread *holder;            // Thread that holds lock
    struct semaphore semaphore;       // Semaphore for blocking/unblocking
    struct list_elem elem;            // For held_locks list
};

struct semaphore {
    unsigned value;                   // Current semaphore value
    struct list waiters;              // Waiters sorted by priority (highest first)
};

struct condition {
    struct list waiters;              // Condition waiters sorted by priority
};

// New structure for condition variable waiters
struct condition_waiter {
    struct semaphore semaphore;       // Semaphore for this waiter
    struct thread *thread;            // Thread waiting on condition
    struct list_elem elem;            // List element for waiters list
};
```

### New Functions

```c
// threads/thread.c
static void thread_donate_priority(struct thread *donor, struct thread *donee, struct lock *lock);
static void thread_remove_donations_for_lock(struct thread *thread, struct lock *lock);
static void thread_update_donated_priority(struct thread *thread);
static bool donation_priority_less(const struct list_elem *a, const struct list_elem *b, void *aux);
static bool thread_priority_greater(const struct list_elem *a, const struct list_elem *b, void *aux);

// threads/synch.c  
static void sema_insert_ordered(struct list *waiters, struct thread *thread);
static void condition_insert_ordered(struct list *waiters, struct condition_waiter *waiter);
```

### Modified Global Variables

```c
// threads/thread.c
static struct list ready_list;        // Ready queue sorted by effective priority (highest first)
```

## Algorithms

### Priority Scheduling

The core scheduler modification involves maintaining the ready queue as a priority queue sorted by effective priority. When `next_thread_to_run()` is called, we simply return the front of the ready list, which contains the highest priority ready thread.

**Thread enqueueing**: When a thread becomes ready (via `thread_unblock()` or `thread_yield()`), we insert it into the ready list using `list_insert_ordered()` with a comparison function that uses effective priority.

**Effective priority calculation**: `thread_get_priority()` returns `max(base_priority, highest_donation_priority)`. Since donations are kept sorted, the highest donation is always at the front of the donations list, making this O(1).

**Priority change handling**: When `thread_set_priority()` is called, we update the base priority and immediately yield if the current thread no longer has the highest effective priority in the system.

### Priority Donation Implementation

**Donation creation**: In `lock_acquire()`, if the lock is held by another thread, we create a donation structure and add it to the holder's donations list (sorted). We then recursively donate through any chain of waiting threads.

**Nested donation**: When thread A donates to B, we check if B is also waiting on a lock (B->donating_to != NULL). If so, we recursively donate A's priority to the thread B is waiting on, continuing until we reach a thread that isn't waiting.

**Donation removal**: In `lock_release()`, we remove all donations associated with that specific lock from the current thread's donations list. After removal, we recalculate the effective priority and yield if necessary.

**Multiple donations**: A thread can receive donations from multiple sources (different locks it holds). We maintain these in a sorted list, so the highest donation is always accessible in O(1) time.

### Synchronization Primitive Modifications

**Semaphores**: The waiters list is maintained as a priority queue. In `sema_up()`, we wake the highest priority waiter instead of the first waiter. In `sema_down()`, we insert the current thread in priority order.

**Condition variables**: Similar to semaphores, we maintain condition waiters in priority order and signal the highest priority waiter first.

**Lock priority inheritance**: When acquiring a lock, if blocked, the current thread donates its priority to the lock holder and potentially through a chain of donations.

### Edge Cases Handled

1. **Circular donation**: Prevented by the constraint that a thread can only donate to one thread at a time (since it blocks on `lock_acquire`).

2. **Multiple lock release**: When a thread holds multiple locks and releases one, only donations associated with that specific lock are removed.

3. **Priority change during donation**: If a thread sets its priority higher than donations, the donations become irrelevant. If lower, donations still apply.

4. **Thread termination**: When a thread terminates, any donations it made are automatically removed as locks are released.

## Synchronization

### Shared Resources

**Ready list (`ready_list`)**: 
- **Access pattern**: Modified in thread scheduling functions, accessed from timer interrupts
- **Protection**: Interrupts disabled during all ready list operations since it's accessed from interrupt context
- **Rationale**: Simple interrupt disabling is sufficient since operations are quick (list insertions/removals)

**Thread donations list (`thread->donations`)**:
- **Access pattern**: Modified during lock acquire/release, read during priority calculations
- **Protection**: Interrupts disabled during donation list modifications
- **Rationale**: Since `thread_get_priority()` can be called from interrupt context for scheduling decisions, we disable interrupts rather than use locks

**Lock holder field (`lock->holder`)**:
- **Access pattern**: Set in `lock_acquire()`, cleared in `lock_release()`, read during donation
- **Protection**: Protected by the lock's semaphore mechanism
- **Rationale**: Only the thread holding the lock can release it, and acquisition is already synchronized by the semaphore

**Semaphore/condition waiters lists**:
- **Access pattern**: Modified when threads block/unblock on synchronization primitives
- **Protection**: Interrupts disabled during waiters list operations
- **Rationale**: These operations are atomic and brief, interrupt disabling provides simple protection

### Memory Management

**Donation structures**: Allocated on the stack in `lock_acquire()` and added to the holder's list. Automatically cleaned up when the donor thread unblocks and the function returns.

**Thread lifecycle**: We don't store pointers to threads across potential thread exits. Donation structures are removed before threads can exit (when locks are released).

### Concurrency Considerations

The design allows high concurrency by using fine-grained interrupt disabling only around critical data structure modifications. Threads can run concurrently in user space while only synchronizing during kernel synchronization primitive operations.

**Contention points**: Primary contention is on the ready list during context switches, but this is inherent to any scheduler. Lock acquisition/release contention is handled by existing semaphore mechanisms.

**Scalability**: The design scales well as most operations (priority lookup, highest priority thread selection) are O(1) due to maintaining sorted lists.

## Rationale

### Design Advantages

1. **Simplicity**: Uses existing list structures and patterns from Pintos. No complex tree structures or hash tables required.

2. **Efficiency**: O(1) priority lookup and highest priority thread selection by maintaining sorted lists.

3. **Correctness**: Explicit tracking of donations per lock prevents bugs during multiple lock scenarios.

4. **Minimal code changes**: Leverages existing scheduling framework, only modifying comparison functions and adding donation tracking.

### Alternative Approaches Considered

**Priority queues with heaps**: Would provide O(log n) insertion but O(1) max element access. Rejected due to implementation complexity and marginal benefit given typical thread counts.

**Single effective priority field**: Initially considered storing computed effective priority, but this creates synchronization issues and potential staleness. The computed approach is cleaner.

**Global donation table**: Could track all donations in a system-wide structure, but per-thread lists provide better locality and simpler cleanup.

### Potential Limitations

1. **Priority queue maintenance**: Frequent priority changes could cause O(n) list reordering, but this is uncommon in typical workloads.

2. **Memory overhead**: Each donation requires a small structure, but this is bounded by the number of concurrent lock waiters.

3. **Nested donation depth**: Deep nesting chains could cause recursive calls, but practical nesting is typically shallow and bounded by the number of locks in the system.

The design prioritizes correctness and simplicity over micro-optimizations, following Pintos' design philosophy of clear, maintainable code.
