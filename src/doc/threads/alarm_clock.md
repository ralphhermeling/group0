# Efficient Alarm Clock Design

## Data Structures and Functions

### Modified Structures

```c
// In threads/thread.h, add to struct thread:
struct thread {
    // ... existing fields ...
    int64_t wake_time;                  /* Time when thread should wake up from sleep */
    struct list_elem sleepelem;        /* List element for sleep_list */
};
```

### New Global Variables

```c
// In devices/timer.c:
static struct list sleep_list;          /* List of sleeping threads ordered by wake_time */
```

### Modified Functions

```c
// In devices/timer.c:
void timer_sleep(int64_t ticks);        /* Block thread instead of busy waiting */
static void timer_interrupt(struct intr_frame*); /* Wake up expired sleeping threads */

// Helper functions:
static bool wake_time_less(const struct list_elem *a, const struct list_elem *b, void *aux);
                                        /* Comparison function for ordered insertion */
static void timer_wake_sleeping_threads(void); /* Wake threads whose time has expired */
```

### Function Modifications

```c
// In threads/thread.c, modify thread initialization:
static void init_thread(struct thread *t, const char *name, int priority);
                                        /* Initialize wake_time and sleep_elem */
```

## Algorithms

### Timer Sleep Implementation

When `timer_sleep(ticks)` is called:

1. **Input Validation**: If `ticks <= 0`, return immediately without sleeping
2. **Calculate Wake Time**: Set `wake_time = timer_ticks() + ticks` for the current thread
3. **Disable Interrupts**: Prevent race conditions with timer interrupt
4. **Insert into Sleep List**: Use `list_insert_ordered()` to maintain sleep_list sorted by wake_time
5. **Block Thread**: Call `thread_block()` to remove thread from ready queue and set status to `THREAD_BLOCKED`
6. **Re-enable Interrupts**: Restore interrupt level

The key insight is that `thread_block()` removes the thread from the scheduler entirely, unlike `thread_yield()` which keeps it in the ready queue.

### Timer Interrupt Modification

In `timer_interrupt()`:

1. **Increment Ticks**: Maintain existing `ticks++` behavior
2. **Wake Sleeping Threads**: 
   - Iterate through sleep_list from front (earliest wake times)
   - For each thread where `wake_time <= current_ticks`:
     - Remove from sleep_list using `list_remove()`
     - Call `thread_unblock()` to add back to ready queue
   - Stop when we find a thread with `wake_time > current_ticks` (list is ordered)
3. **Continue Normal Processing**: Call existing `thread_tick()`

### Ordering Strategy

The `wake_time_less()` function compares wake_time fields to maintain sleep_list in ascending order. This ensures we only need to check threads at the front of the list in the interrupt handler, making wake-up checks O(k) where k is the number of threads to wake, rather than O(n) for all sleeping threads.

### Thread Creation

In `init_thread()`, initialize new fields:
- Set `wake_time = 0` (thread not sleeping)
- Initialize `sleep_elem` using `list_elem_init()`

## Synchronization

### Shared Resources

1. **sleep_list**: Accessed by `timer_sleep()` (thread context) and `timer_interrupt()` (interrupt context)
2. **struct thread fields**: `wake_time` and `sleep_elem` accessed by both contexts
3. **Global ticks**: Read by `timer_sleep()`, incremented by `timer_interrupt()`

### Synchronization Strategy

**Interrupt Disabling Approach**: Since `timer_interrupt()` runs with interrupts disabled and cannot acquire locks, we use interrupt disabling in `timer_sleep()` to create atomic critical sections.

```c
void timer_sleep(int64_t ticks) {
    enum intr_level old_level = intr_disable();
    // Critical section: modify sleep_list and thread fields
    intr_set_level(old_level);
}
```

**Why This Works**:
- `timer_interrupt()` already runs with interrupts disabled
- Disabling interrupts in `timer_sleep()` prevents preemption by timer interrupt
- No deadlock possible since we never acquire locks
- Short critical sections minimize interrupt latency

**Memory Safety**: Threads cannot be deallocated while sleeping because:
- Blocked threads remain in `all_list` until they call `thread_exit()`
- `thread_exit()` requires the thread to be running (unblocked)
- We only access thread structs that are in our sleep_list

### Concurrency Analysis

**Contention**: Minimal contention since:
- Only sleeping threads contend on sleep_list insertion
- Timer interrupt has exclusive access during wake-up checks
- No locks means no blocking of non-sleeping threads

**Scalability**: Algorithm scales well:
- Insertion: O(n) where n is number of sleeping threads
- Wake-up: O(k) where k is threads waking this tick
- No impact on threads not using timer_sleep()

### Race Condition Prevention

**Timer Sleep vs. Timer Interrupt**: 
- Interrupt disable in `timer_sleep()` prevents timer interrupt from firing during sleep_list modification
- Once thread is blocked, it cannot be scheduled until unblocked by timer interrupt

**Multiple Timer Sleep Calls**: 
- Each thread has its own `wake_time` and `sleep_elem`
- Interrupt disabling provides mutual exclusion for sleep_list modifications

## Rationale

### Why Separate Sleep List vs. Using Existing Blocked Thread Infrastructure

**Separate Sleep List Advantages**:
- **Efficient Wake-up**: O(k) time to wake sleeping threads vs. O(n) scanning all blocked threads
- **Ordered by Time**: Sleep list maintains temporal ordering, blocked threads list doesn't
- **Clear Separation**: Sleep-specific logic isolated from general thread blocking
- **Timer Interrupt Efficiency**: Critical since interrupt handlers must be fast

**Alternative Considered**: Scanning all blocked threads in `timer_interrupt()` to find expired sleepers would be O(n) for every timer tick, significantly impacting system performance.

### Sleeping Threads Use THREAD_BLOCKED Status

Sleeping threads correctly use the existing `THREAD_BLOCKED` status because:
- Sleep is semantically a form of blocking (waiting for time event)
- Reuses existing `thread_block()` and `thread_unblock()` infrastructure
- No new thread states needed - keeps design simple
- Blocked threads are automatically excluded from scheduling

### Advantages Over Current Implementation

1. **Eliminates Busy Waiting**: Current implementation wastes CPU cycles continuously checking time
2. **Better CPU Utilization**: Blocked threads don't consume scheduler time
3. **Scalability**: O(1) per sleeping thread in scheduler vs. O(n) repeated scheduling
4. **Power Efficiency**: CPU can idle when all threads are blocked

### Advantages Over Alternative Designs

**vs. No Separate Sleep List**: 
- Avoids O(n) scan of all blocked threads every timer interrupt
- Maintains temporal ordering for efficient processing
- Separates sleep-specific data from general thread blocking

**vs. Semaphore-based Approach**: 
- Simpler implementation without additional synchronization primitives
- Lower memory overhead (no semaphore per sleeping thread)
- Direct integration with existing thread blocking mechanism

### Design Trade-offs

**Time Complexity**:
- Sleep: O(n) for ordered insertion where n = sleeping threads
- Wake: O(k) where k = threads waking this tick
- Space: O(1) additional space per thread

**Limitations**:
- Ordered list insertion could be slow with many sleeping threads
- Could be optimized with priority queue/heap for better insertion time
- Current design prioritizes simplicity over absolute optimal performance

### Extension Considerations

The design easily accommodates future features:
- **Priority-based Wake-up**: Modify comparison function to consider priority
- **Periodic Timers**: Add repeat count field to thread structure  
- **High-resolution Timers**: Change wake_time to higher precision type
- **Timer Cancellation**: Add function to remove thread from sleep_list early

The modular design with clear separation between sleep list management and thread blocking makes extensions straightforward without major architectural changes.
