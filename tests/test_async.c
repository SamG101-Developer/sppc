// Regression test: the green-thread runtime is shared across translation
// units, and spawn/await round-trips correctly.
//
// The scheduler state used to be `static` in async.h, so every TU including
// the header got a private scheduler (and a private 15MB task_pool). A task
// spawned in one TU was invisible to another.

#include <sppc/sppc.h>
#include "harness.h"

extern int tu2_spawn(size_t *handle, void *(*fn)(size_t, uintptr_t const *), uintptr_t a, uintptr_t b);
extern void *tu2_task_pool(void);
extern int tu2_task_free(void);

static void *add(size_t argc, uintptr_t const *argv) {
  size_t sum = 0;
  for (size_t i = 0; i < argc; i++) sum += argv[i];
  return (void*)(uintptr_t)sum;
}

static void *ret_const(size_t argc, uintptr_t const *argv) {
  (void)argc; (void)argv;
  return (void*)(uintptr_t)7;
}

int main(void) {
  TEST_BEGIN("async runtime");
  sppc_init();
  char detail[96];

  size_t h = 0;
  CHECK_EQ("sppc_async returns 0", sppc_async(&h, add, (size_t)3,
           (uintptr_t)10, (uintptr_t)20, (uintptr_t)12), 0);

  // Checked after a spawn: the pool is mapped on first use, so before one both
  // sides would read NULL and match for the wrong reason.
  snprintf(detail, sizeof detail, "tu1=%p tu2=%p", (void*)gt_task_pool, tu2_task_pool());
  CHECK("task_pool is one object across TUs",
        gt_task_pool != NULL && tu2_task_pool() == (void*)gt_task_pool, detail);
  CHECK_EQ("await sums 10+20+12", (size_t)(uintptr_t)sppc_await(h), 42);

  // Spawned in the other TU, awaited here.
  size_t h2 = 0;
  CHECK_EQ("cross-TU spawn returns 0", tu2_spawn(&h2, add, 40, 2), 0);
  CHECK_EQ("cross-TU await", (size_t)(uintptr_t)sppc_await(h2), 42);

  const int before = gt_task_free;
  CHECK_EQ("TU2 sees the same task counter", tu2_task_free(), before);

  // Many sequential tasks.
  int all_ok = 1;
  for (int i = 0; i < 64; i++) {
    size_t hi = 0;
    if (sppc_async(&hi, ret_const, (size_t)0) != 0) { all_ok = 0; break; }
    if ((size_t)(uintptr_t)sppc_await(hi) != 7) { all_ok = 0; break; }
  }
  CHECK("64 sequential spawn/await round-trips", all_ok, all_ok ? "" : "a round-trip failed");

  // Too many arguments is rejected rather than overrunning argv.
  size_t h3 = 0;
  const int big = sppc_async(&h3, add, (size_t)(GT_MAX_ARGS + 1));
  snprintf(detail, sizeof detail, "rc=%d E2BIG=%d", big, E2BIG);
  CHECK("argc > GT_MAX_ARGS rejected", big == E2BIG, detail);

  return TEST_SUMMARY();
}