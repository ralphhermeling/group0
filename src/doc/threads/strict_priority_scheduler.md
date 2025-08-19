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
```

### New Functions

```c
// threads/thread.c
static bool donation_priority_greater(const struct list_elem *a, const struct list_elem *b, void *aux);
static donation* find_donation_by_donor_and_donee(struct thread* donor,struct thread* donee);
static bool thread_priority_greater(const struct list_elem *a, const struct list_elem *b, void *aux);

// threads/synch.c  
static void sema_insert_ordered(struct list *waiters, struct thread *thread);
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

**Donation removal**: In `lock_release()`, we remove all donations associated with that specific lock from the current thread's donations list. The sorted donations list automatically maintains correct effective priority calculation.

**Multiple donations**: A thread can receive donations from multiple sources (different locks it holds). We maintain these in a sorted list, so the highest donation is always accessible in O(1) time.

### Lock Acquire/Release Implementation

```c
void lock_acquire(struct lock *lock) {
    struct thread *current = thread_current();
    
    // Disable interrupts for atomic donation operations
    enum intr_level old_level = intr_disable();
    
    // If lock is held, donate priority and set up donation chain
    if (lock->holder != NULL) {
        // Create donation structure on stack
        struct donation donation;
        donation.donated_priority = thread_get_priority(current);
        donation.donor = current;
        donation.lock = lock;
        
        // Add donation to holder's sorted donations list
        list_insert_ordered(&lock->holder->donations, &donation.elem, 
                           donation_priority_greater, NULL);
        
        // Set up chain for nested donation
        current->donating_to = lock->holder;
        
        // Recursively donate through the chain
        struct thread *recipient = lock->holder;
        while (recipient->donating_to != NULL) {
            struct thread *next = recipient->donating_to;
            
            // Find existing donation from recipient to next
            struct donation *existing = find_donation_by_donor_and_donee(recipient, next);
            if (existing && existing->donated_priority < donation.donated_priority) {
                // Update existing donation with higher priority
                existing->donated_priority = donation.donated_priority;
                // Re-sort donations list
                list_sort(&next->donations, donation_priority_greater, NULL);
            }
            recipient = next;
        }
    }
    
    intr_set_level(old_level);
    
    // Block until lock is available
    sema_down(&lock->semaphore);
    
    // Acquired the lock
    lock->holder = current;
    current->donating_to = NULL; // No longer donating
    
    // Add lock to held_locks list
    list_push_back(&current->held_locks, &lock->elem);
}

void lock_release(struct lock *lock) {
    struct thread *current = thread_current();
    
    // Disable interrupts for atomic operations
    enum intr_level old_level = intr_disable();
    
    // Remove this lock from held_locks list
    list_remove(&lock->elem);
    
    // Remove all donations associated with this specific lock
    struct list_elem *e = list_begin(&current->donations);
    while (e != list_end(&current->donations)) {
        struct donation *d = list_entry(e, struct donation, elem);
        struct list_elem *next = list_next(e);
        
        if (d->lock == lock) {
            list_remove(e);
        }
        e = next;
    }
    
    // Clear lock holder
    lock->holder = NULL;
    
    intr_set_level(old_level);
    
    // Wake up highest priority waiter
    sema_up(&lock->semaphore);
    
    // Yield if current thread no longer has highest effective priority
    if (!list_empty(&ready_list)) {
        struct thread *highest = list_entry(list_front(&ready_list), 
                                           struct thread, elem);
        if (thread_get_priority(current) < thread_get_priority(highest)) {
            thread_yield();
        }
    }
}
```

### Recursive Donation Example

Let's trace through a concrete example to verify our solution handles nested donations correctly:

**Scenario**: Three threads with a chain of lock dependencies:
- Thread H (priority 60) wants Lock A, held by M
- Thread M (priority 30) wants Lock B, held by L  
- Thread L (priority 10) runs

**Step 1: H calls `lock_acquire(Lock A)`**
```
H blocks on Lock A (held by M)
- Create donation: {priority: 60, donor: H, lock: Lock A}
- Add to M->donations: [donation(60, H, Lock A)]
- H->donating_to = M
- M's effective priority = max(30, 60) = 60

Recursive donation:
- M->donating_to = L (M is waiting on Lock B)
- Find M's existing donation to L for Lock B
- Update M's donation to L: {priority: 60, donor: M, lock: Lock B}
- L->donations: [donation(60, M, Lock B)]
- L's effective priority = max(10, 60) = 60
```

**Step 2: Current state after H blocks**
```
Thread priorities:
- H: blocked (base: 60)
- M: effective 60 (base: 30, donated: 60 from H)  
- L: effective 60 (base: 10, donated: 60 from M)

Donation chains:
- H donates 60 to M for Lock A
- M donates 60 to L for Lock B (propagated from H's donation)

Running: L (priority 60)
```

**Step 3: L releases Lock B**
```
L calls lock_release(Lock B):
- Remove donations for Lock B from L->donations
- L->donations becomes empty []
- L's effective priority = max(10, none) = 10
- M gets Lock B, M->donating_to = NULL
- M's effective priority = max(30, 60) = 60 (still has H's donation)

Running: M (priority 60)
```

**Step 4: M releases Lock A**
```
M calls lock_release(Lock A):
- Remove donations for Lock A from M->donations  
- M->donations becomes empty []
- M's effective priority = max(30, none) = 30
- H gets Lock A, H->donating_to = NULL
- H's effective priority = 60 (base priority)

Running: H (priority 60)
```

**Verification**: Our algorithm correctly handles:
1. ✅ **Nested donation creation**: H's priority propagates H→M→L
2. ✅ **Selective removal**: Only donations for the specific released lock are removed
3. ✅ **Automatic chain updates**: No manual chain updates needed - computed effective priority handles it
4. ✅ **Correct priorities**: Each thread runs at appropriate effective priority
5. ✅ **Clean unwinding**: Donations are removed in correct order as locks release

### Synchronization Primitive Modifications

**Semaphores**: The waiters list is maintained as a priority queue using the existing `thread->elem` field. In `sema_up()`, we wake the highest priority waiter instead of the first waiter. In `sema_down()`, we insert the current thread in priority order using `list_insert_ordered()`.

**Condition variables**: The waiters list contains threads directly (not wrapper structures) sorted by effective priority using `thread->elem`. Since a thread can only be in one waiting state at a time (ready, blocked on semaphore, or blocked on condition), we can reuse the same list element field. In `cond_signal()`, we wake the highest priority waiter, and in `cond_wait()`, we insert threads in priority order.

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

5. **Memory efficiency**: Reuses existing `thread->elem` field for all waiting lists instead of creating wrapper structures, reducing memory overhead and complexity.

### Alternative Approaches Considered

**Priority queues with heaps**: Would provide O(log n) insertion but O(1) max element access. Rejected due to implementation complexity and marginal benefit given typical thread counts.

**Single effective priority field**: Initially considered storing computed effective priority, but this creates synchronization issues and potential staleness. The computed approach is cleaner.

**Global donation table**: Could track all donations in a system-wide structure, but per-thread lists provide better locality and simpler cleanup.

**Separate condition waiter structures**: Initially considered creating wrapper structures for condition variable waiters, but reusing `thread->elem` is more efficient since threads can only be in one waiting state at a time.

### Potential Limitations

1. **Priority queue maintenance**: Frequent priority changes could cause O(n) list reordering, but this is uncommon in typical workloads.

2. **Memory overhead**: Each donation requires a small structure, but this is bounded by the number of concurrent lock waiters.

3. **Nested donation depth**: Deep nesting chains could cause recursive calls, but practical nesting is typically shallow and bounded by the number of locks in the system.

The design prioritizes correctness and simplicity over micro-optimizations, following Pintos' design philosophy of clear, maintainable code.
