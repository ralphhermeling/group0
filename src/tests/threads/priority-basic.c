/* Tests basic priority scheduling without donations.
   Creates three threads with different priorities and verifies
   they run in priority order. */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/thread.h"

static thread_func high_priority_thread;
static thread_func medium_priority_thread;
static thread_func low_priority_thread;

static int execution_order[3];
static int execution_count = 0;

void test_priority_basic(void) {
  for (int i = 0; i < 3; i++) {
    execution_order[i] = -1;
  }
  /* This test requires priority scheduling. */
  ASSERT(active_sched_policy == SCHED_PRIO);

  /* Make sure our priority is the default. */
  ASSERT(thread_get_priority() == PRI_DEFAULT);

  execution_count = 0;

  msg("Creating threads with different priorities...");
  thread_create("low", PRI_DEFAULT + 1, low_priority_thread, NULL);
  thread_create("high", PRI_DEFAULT + 3, high_priority_thread, NULL);
  thread_create("medium", PRI_DEFAULT + 2, medium_priority_thread, NULL);

  /* Wait a bit for threads to complete */
  int i;
  for (i = 0; i < 1000; i++) {
    thread_yield();
  }

  msg("Execution order: %d, %d, %d", execution_order[0], execution_order[1], execution_order[2]);

  /* High priority (32) should run first, medium (31) second, low (30) third */
  ASSERT(execution_order[0] == PRI_DEFAULT + 3); /* high */
  ASSERT(execution_order[1] == PRI_DEFAULT + 2); /* medium */
  ASSERT(execution_order[2] == PRI_DEFAULT + 1); /* low */

  msg("Basic priority scheduling works correctly!");
}

static void high_priority_thread(void* aux UNUSED) {
  execution_order[execution_count++] = thread_get_priority();
  msg("High priority thread (priority %d) running", thread_get_priority());
  thread_yield(); /* Yield to allow other threads to run */
}

static void medium_priority_thread(void* aux UNUSED) {
  execution_order[execution_count++] = thread_get_priority();
  msg("Medium priority thread (priority %d) running", thread_get_priority());
  thread_yield(); /* Yield to allow other threads to run */
}

static void low_priority_thread(void* aux UNUSED) {
  execution_order[execution_count++] = thread_get_priority();
  msg("Low priority thread (priority %d) running", thread_get_priority());
  thread_yield(); /* Yield to allow other threads to run */
}
