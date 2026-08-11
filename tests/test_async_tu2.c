// Second translation unit for test_async. Exists to prove that a TU which
// includes <sppc/sppc.h> shares the one green-thread runtime defined in
// sources/sppc/sppc.c, rather than getting a private copy of the scheduler.

#include <sppc/sppc.h>

int tu2_spawn(size_t *handle, void *(*fn)(size_t, uintptr_t const *), uintptr_t a, uintptr_t b) {
  return sppc_async(handle, fn, (size_t)2, a, b);
}

void *tu2_task_pool(void) { return (void*)gt_task_pool; }

int tu2_task_free(void) { return gt_task_free; }