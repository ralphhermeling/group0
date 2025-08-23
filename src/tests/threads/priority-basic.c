/* Tests basic priority scheduling without donations.
   Creates three threads with different priorities, makes them all ready
   simultaneously, and verifies they run in strict priority order. */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/thread.h"
#include "threads/synch.h"

static thread_func high_priority_thread;
static thread_func medium_priority_thread;
static thread_func low_priority_thread;

static int execution_order[3];
static int execution_count = 0;
static struct semaphore start_sema;

void test_priority_basic(void) {
  for (int i = 0; i < 3; i++) {
    execution_order[i] = -1;
  }
  /* This test requires priority scheduling. */
  ASSERT(active_sched_policy == SCHED_PRIO);

  /* Make sure our priority is the default. */
  ASSERT(thread_get_priority() == PRI_DEFAULT);

  execution_count = 0;

  /* Initialize semaphore - all threads will wait on this */
  sema_init(&start_sema, 0);

  msg("Creating threads with different priorities...");
  /* Create all threads - they'll each run briefly then wait on semaphore */
  thread_create("low", PRI_DEFAULT + 1, low_priority_thread, NULL);
  thread_create("medium", PRI_DEFAULT + 2, medium_priority_thread, NULL);
  thread_create("high", PRI_DEFAULT + 3, high_priority_thread, NULL);

  msg("All threads created and waiting. Now releasing them simultaneously...");

  /* Release all threads at once - now they compete based on priority */
  sema_up(&start_sema);
  sema_up(&start_sema);
  sema_up(&start_sema);
  sema_up(&start_sema);

  /* Wait for all threads to complete */
  int i;
  for (i = 0; i < 1000; i++) {
    thread_yield();
  }

  msg("Execution order: %d, %d, %d", execution_order[0], execution_order[1], execution_order[2]);

  /* Now they should run in strict priority order: high, medium, low */
  ASSERT(execution_order[0] == PRI_DEFAULT + 3); /* high runs first */
  ASSERT(execution_order[1] == PRI_DEFAULT + 2); /* medium runs second */
  ASSERT(execution_order[2] == PRI_DEFAULT + 1); /* low runs last */

  msg("Basic priority scheduling works correctly!");
}

static void high_priority_thread(void* aux UNUSED) {
  /* Wait for main thread to release all threads simultaneously */
  sema_down(&start_sema);
  execution_order[execution_count++] = thread_get_priority();
  msg("High priority thread (priority %d) running", thread_get_priority());
}

static void medium_priority_thread(void* aux UNUSED) {
  /* Wait for main thread to release all threads simultaneously */
  sema_down(&start_sema);
  execution_order[execution_count++] = thread_get_priority();
  msg("Medium priority thread (priority %d) running", thread_get_priority());
}

static void low_priority_thread(void* aux UNUSED) {
  /* Wait for main thread to release all threads simultaneously */
  sema_down(&start_sema);
  execution_order[execution_count++] = thread_get_priority();
  msg("Low priority thread (priority %d) running", thread_get_priority());
}
