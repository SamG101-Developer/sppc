// Regression test: sppc_pthread_create runs the routine on a real thread.
//
// It used to cast the void(*)(void) start routine to pthread's
// void*(*)(void*) and call through it, which is undefined -- the callee reads
// a parameter that was never passed and returns nothing where a value is
// expected. It works on x86-64 SysV by accident. A trampoline calls the
// routine at its own type instead.

#include <sppc/sppc.h>
#include "harness.h"

static int ran = 0;
static uint64_t thread_id = 0;
static void record(void) {
  ran++;
  thread_id = pthread_self();
}

static int counter = 0;
static uint64_t counter_mutex[8];
static void bump(void) {
  for (int i = 0; i < 1000; i++) {
    sppc_pthread_mutex_lock(counter_mutex);
    counter++;
    sppc_pthread_mutex_unlock(counter_mutex);
  }
}

int main(void) {
  TEST_BEGIN("pthread_create");
  sppc_init();
  char detail[128];

  uint64_t self = 0;
  sppc_pthread_self(&self);

  uint64_t handle = 0;
  CHECK_EQ("create", sppc_pthread_create(record, &handle), 0);
  CHECK_EQ("join", sppc_pthread_join(&handle), 0);
  snprintf(detail, sizeof detail, "ran=%d", ran);
  CHECK("routine ran exactly once", ran == 1, detail);
  CHECK("  ran on a different thread", thread_id != 0 && thread_id != self, "distinct tid");

  // --- several threads contending through the wrappers ---
  sppc_pthread_mutex_init(counter_mutex);
  uint64_t handles[8];
  int created = 0;
  for (int i = 0; i < 8; i++) {
    if (sppc_pthread_create(bump, &handles[i]) == 0) created++;
  }
  CHECK_EQ("created 8 threads", created, 8);
  int joined = 0;
  for (int i = 0; i < 8; i++) {
    if (sppc_pthread_join(&handles[i]) == 0) joined++;
  }
  CHECK_EQ("joined 8 threads", joined, 8);
  snprintf(detail, sizeof detail, "counter=%d expected=%d", counter, 8 * 1000);
  CHECK("every increment landed", counter == 8 * 1000, detail);
  sppc_pthread_mutex_destroy(counter_mutex);

  // --- detach path ---
  uint64_t detached = 0;
  CHECK_EQ("create (to detach)", sppc_pthread_create(record, &detached), 0);
  CHECK_EQ("detach", sppc_pthread_detach(&detached), 0);

  // --- self/equal ---
  uint64_t a = 0, b = 0, eq = 0;
  sppc_pthread_self(&a);
  sppc_pthread_self(&b);
  sppc_pthread_equal(&a, &b, &eq);
  CHECK_EQ("pthread_equal(self, self)", (int)eq, 1);

  return TEST_SUMMARY();
}