// Regression test: each pthread runs its own green-thread scheduler.
//
// The scheduler state was global while the library also hands out
// sppc_pthread_create, so `go` from two OS threads raced on the run queue,
// the free list and gt_current. It is thread-local now: pthreads are the
// OS-level threading primitive, and each one drives an independent `go`
// runtime, so there is no shared state to race on.
//
// A task is spawned and awaited on the same thread; S++ enforces that by
// making the Future type non-thread-sharable.

#include <sppc/sppc.h>
#include "harness.h"

#define WORKERS 8
#define TASKS_PER_WORKER 500

static void *add_args(size_t argc, uintptr_t const *argv) {
  size_t sum = 0;
  for (size_t i = 0; i < argc; i++) sum += argv[i];
  return (void*)(uintptr_t)sum;
}

typedef struct {
  int id;
  int ok;
  void *pool;
  int peak_slots;
} worker;

static worker workers[WORKERS];

static void *run_worker(void *arg) {
  worker *w = arg;
  w->ok = 1;

  for (int i = 0; i < TASKS_PER_WORKER; i++) {
    size_t h = 0;
    const uintptr_t a = (uintptr_t)(w->id * 1000 + i);
    if (sppc_async(&h, add_args, (size_t)2, a, (uintptr_t)7) != 0) { w->ok = 0; break; }
    if ((size_t)(uintptr_t)sppc_await(h) != (size_t)(a + 7)) { w->ok = 0; break; }
  }

  // Recorded after the work, so the pool has been mapped for this thread.
  w->pool = (void*)gt_task_pool;
  w->peak_slots = gt_task_free;
  return NULL;
}

int main(void) {
  TEST_BEGIN("per-thread schedulers");
  sppc_init();
  char detail[160];

  // Give the main thread a task first, so its pool is mapped and distinct.
  size_t mh = 0;
  sppc_async(&mh, add_args, (size_t)1, (uintptr_t)1);
  sppc_await(mh);
  void *main_pool = (void*)gt_task_pool;
  CHECK("main thread has a pool", main_pool != NULL, "mapped on first use");

  pthread_t th[WORKERS];
  for (int i = 0; i < WORKERS; i++) {
    workers[i].id = i + 1;
    pthread_create(&th[i], NULL, run_worker, &workers[i]);
  }
  for (int i = 0; i < WORKERS; i++) pthread_join(th[i], NULL);

  int all_ok = 1, all_mapped = 1;
  for (int i = 0; i < WORKERS; i++) {
    if (!workers[i].ok) all_ok = 0;
    if (workers[i].pool == NULL) all_mapped = 0;
  }
  snprintf(detail, sizeof detail, "%d threads x %d tasks", WORKERS, TASKS_PER_WORKER);
  CHECK("every task returned its own value", all_ok, detail);
  CHECK("every worker mapped a pool", all_mapped, "");

  // The point of the fix: no two schedulers share state.
  int distinct = 1;
  for (int i = 0; i < WORKERS && distinct; i++) {
    if (workers[i].pool == main_pool) { distinct = 0; break; }
    for (int j = i + 1; j < WORKERS; j++)
      if (workers[i].pool == workers[j].pool) { distinct = 0; break; }
  }
  snprintf(detail, sizeof detail, "main=%p w1=%p w2=%p",
           main_pool, workers[0].pool, workers[1].pool);
  CHECK("each thread has its own pool", distinct, detail);

  // Slot recycling is per-thread too: 500 sequential tasks should reuse one.
  int max_slots = 0;
  for (int i = 0; i < WORKERS; i++)
    if (workers[i].peak_slots > max_slots) max_slots = workers[i].peak_slots;
  snprintf(detail, sizeof detail, "worst thread used %d slot(s) for %d tasks",
           max_slots, TASKS_PER_WORKER);
  CHECK("slots recycle within each thread", max_slots <= 2, detail);

  // The main thread's own scheduler is untouched by all of that.
  size_t after = 0;
  sppc_async(&after, add_args, (size_t)2, (uintptr_t)20, (uintptr_t)22);
  CHECK_EQ("main thread still works", (size_t)(uintptr_t)sppc_await(after), 42);
  snprintf(detail, sizeof detail, "pool=%p", (void*)gt_task_pool);
  CHECK("main thread pool unchanged", (void*)gt_task_pool == main_pool, detail);

  return TEST_SUMMARY();
}