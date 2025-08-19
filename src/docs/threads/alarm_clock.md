# Alarm clock design

For the alarm clock, the timer interrupt needs to wake up sleeping threads.
When you access these variables from kernel threads, you will need to disable interrupts to prevent the timer interrupt from interfering.
```c
enum intr_level intr_disable (void);
enum intr_level intr_enable (void);
```

There should be no busy waiting in your submission.
A tight loop that calls thread_yield() is one form of busy waiting.

A list of sleeping threads sorted by wake time.
struct list sleep_list;

So we should create an extra property on the thread struct called uint64_t wake_time.

When we receive a timer interrupt we iterate through the list of sleeping threads,
and we check which thread wake time has passed.
We move these threads to the ready list.

When timer_sleep is called we should disable interrupts
update the current thread's wake time to be timer_ticks() + ticks

add thread to the sleep list
remove thread from the ready list

enable interrupts
