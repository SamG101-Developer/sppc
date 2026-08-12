// Regression test: task slots must be recycled, and stale handles rejected.
//
// gt_task_free only ever incremented, so the pool was consumed permanently:
// the 65537th spawn of a process aborted however many tasks had finished.
// Slots are reused now, which makes a stale handle able to address a task
// that has since been reused -- so a handle carries the slot's generation
// and stops resolving once the slot is retired.

#include <sppc/sppc.h>
#include "harness.h"

static void *ret_n(size_t argc, uintptr_t const *argv) {
  return (void*)(uintptr_t)(argc ? argv[0] : 0);
}

int main(void) {
  TEST_BEGIN("gt slot recycling");
  sppc_init();
  char detail[128];

  // --- a completed task's slot comes back ---
  const int before = gt_task_free;
  size_t h = 0;
  sppc_async(&h, ret_n, (size_t)1, (uintptr_t)11);
  CHECK_EQ("await returns the value", (size_t)(uintptr_t)sppc_await(h), 11);
  snprintf(detail, sizeof detail, "gt_task_free %d -> %d", before, gt_task_free);
  CHECK("pool did not grow past one slot", gt_task_free <= before + 1, detail);

  // --- many sequential tasks reuse the same slot ---
  const int mark = gt_task_free;
  int ok = 1;
  for (int i = 0; i < 5000; i++) {
    size_t hi = 0;
    if (sppc_async(&hi, ret_n, (size_t)1, (uintptr_t)i) != 0) { ok = 0; break; }
    if ((size_t)(uintptr_t)sppc_await(hi) != (size_t)i) { ok = 0; break; }
  }
  snprintf(detail, sizeof detail, "5000 tasks grew the pool by %d slot(s)", gt_task_free - mark);
  CHECK("5000 spawn/await round-trips succeed", ok, ok ? "" : "a round-trip failed");
  CHECK("  pool did not grow", gt_task_free - mark == 0, detail);

  // --- concurrent tasks each need their own slot ---
  size_t hs[16];
  for (int i = 0; i < 16; i++) sppc_async(&hs[i], ret_n, (size_t)1, (uintptr_t)(100 + i));
  int distinct = 1;
  for (int i = 0; i < 16 && distinct; i++)
    for (int j = i + 1; j < 16; j++)
      if (hs[i] == hs[j]) { distinct = 0; break; }
  CHECK("live tasks hold distinct handles", distinct, distinct ? "" : "two live tasks shared a slot");

  int all = 1;
  for (int i = 0; i < 16; i++) {
    if ((size_t)(uintptr_t)sppc_await(hs[i]) != (size_t)(100 + i)) all = 0;
  }
  CHECK("all 16 return their own value", all, all ? "" : "a value was wrong");

  // --- a stale handle does not resolve to a recycled task ---
  size_t stale = 0;
  sppc_async(&stale, ret_n, (size_t)1, (uintptr_t)777);
  CHECK_EQ("first await gets the result", (size_t)(uintptr_t)sppc_await(stale), 777);

  // That slot is now free; the next spawn takes it.
  size_t fresh = 0;
  sppc_async(&fresh, ret_n, (size_t)1, (uintptr_t)888);
  snprintf(detail, sizeof detail, "stale=%#zx fresh=%#zx", stale, fresh);
  CHECK("recycled slot yields a different handle", stale != fresh, detail);

  void *stale_result = sppc_await(stale);
  snprintf(detail, sizeof detail, "stale await -> %p", stale_result);
  CHECK("stale handle returns NULL, not the new task's result", stale_result == NULL, detail);

  CHECK_EQ("the live handle still works", (size_t)(uintptr_t)sppc_await(fresh), 888);

  // --- a handle is never 0, and garbage does not resolve ---
  size_t probe = 0;
  sppc_async(&probe, ret_n, (size_t)0);
  CHECK("a valid handle is non-zero", probe != 0, "generations start at 1");
  sppc_await(probe);
  CHECK("handle 0 returns NULL", sppc_await(0) == NULL, "rejected");
  CHECK("out-of-range handle returns NULL", sppc_await(~(size_t)0) == NULL, "rejected");

  return TEST_SUMMARY();
}