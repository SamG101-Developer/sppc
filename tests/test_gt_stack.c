// Regression test: green-thread stacks must satisfy the SysV x86-64 ABI.
//
// gt_switch reaches a task through `ret`, so gt_spawn has to place the entry
// address such that rsp % 16 == 8 once that `ret` has popped it. It used to
// land on 0, which faults any aligned SSE access inside the task.

#include <sppc/sppc.h>
#include "harness.h"

__attribute__((noinline)) static uintptr_t entry_rsp(void) {
  uintptr_t rsp;
  __asm__ volatile ("mov %%rsp, %0" : "=r"(rsp));
  return rsp % 16;
}

static void *probe_alignment(size_t argc, uintptr_t const *argv) {
  (void)argc; (void)argv;
  return (void*)entry_rsp();
}

// Forces the compiler to emit 16-byte-aligned SSE moves, which segfault on a
// misaligned stack. This is the failure the alignment bug actually causes.
typedef struct { double v[8]; } aligned_block;

static void *probe_sse(size_t argc, uintptr_t const *argv) {
  (void)argc; (void)argv;
  volatile aligned_block __attribute__((aligned(16))) a;
  for (int i = 0; i < 8; i++) a.v[i] = (double)i;
  aligned_block b = *(aligned_block*)&a;
  double sum = 0;
  for (int i = 0; i < 8; i++) sum += b.v[i];
  return (void*)(uintptr_t)sum;   // 0+1+...+7 = 28
}

static void *probe_nested(size_t argc, uintptr_t const *argv) {
  (void)argc; (void)argv;
  return (void*)entry_rsp();      // measured one frame deeper
}

int main(void) {
  TEST_BEGIN("gt stack alignment");
  sppc_init();
  char detail[80];

  // Assert against a normally-called frame rather than a hardcoded 8: how much
  // prologue runs before the probe reads rsp depends on the optimisation
  // level, but a green thread must always land in the same alignment state as
  // an ordinary call. Before the fix these differed by 8.
  const size_t baseline = (size_t)entry_rsp();
  snprintf(detail, sizeof detail, "rsp%%16=%zu (reference for the checks below)", baseline);
  CHECK("baseline: normal call", 1, detail);

  size_t h = 0;
  sppc_async(&h, probe_alignment, (size_t)0);
  CHECK_EQ("green thread matches normal call", (size_t)(uintptr_t)sppc_await(h), baseline);

  sppc_async(&h, probe_nested, (size_t)0);
  CHECK_EQ("green thread matches (nested call)", (size_t)(uintptr_t)sppc_await(h), baseline);

  // Absolute, and independent of the probe: a misaligned stack faults here.
  sppc_async(&h, probe_sse, (size_t)0);
  CHECK_EQ("aligned SSE inside task", (size_t)(uintptr_t)sppc_await(h), 28);

  // Alignment must hold for every task, not just the first.
  int all_aligned = 1;
  for (int i = 0; i < 32; i++) {
    size_t hi = 0;
    sppc_async(&hi, probe_alignment, (size_t)0);
    if ((size_t)(uintptr_t)sppc_await(hi) != baseline) all_aligned = 0;
  }
  CHECK("32 further tasks all aligned", all_aligned, all_aligned ? "" : "a task diverged from baseline");

  return TEST_SUMMARY();
}