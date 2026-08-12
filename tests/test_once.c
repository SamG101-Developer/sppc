// Regression test: sppc_pthread_once must run its function exactly once.
//
// The once-control used to be a function-local `constexpr` inside the wrapper,
// so every call got a fresh control and func ran every time -- the one thing
// pthread_once exists to prevent. (Passing a constexpr to a function that
// writes through it was undefined behaviour on top of that.) The control now
// lives in a caller-owned handle, like the other sync objects.

#include <sppc/sppc.h>
#include "harness.h"

static int counter_a = 0;
static int counter_b = 0;
static void bump_a(void) { counter_a++; }
static void bump_b(void) { counter_b++; }

#define THREADS 8
#define PER_THREAD 50

static uint64_t shared_once[8];
static int shared_counter = 0;
static void bump_shared(void) { shared_counter++; }

static void *hammer(void *arg) {
  (void)arg;
  for (int i = 0; i < PER_THREAD; i++) sppc_pthread_once(shared_once, bump_shared);
  return NULL;
}

int main(void) {
  TEST_BEGIN("pthread_once");
  sppc_init();
  char detail[96];

  // --- repeated calls on one handle run func once ---
  uint64_t once_a[8];
  CHECK_EQ("once_init", sppc_pthread_once_init(once_a), 0);
  for (int i = 0; i < 5; i++) {
    errno = 12345;
    CHECK_EQ("  once returns 0", sppc_pthread_once(once_a, bump_a), 0);
  }
  snprintf(detail, sizeof detail, "ran %d time(s)", counter_a);
  CHECK("func ran exactly once across 5 calls", counter_a == 1, detail);

  // --- a second, independent handle is unaffected by the first ---
  uint64_t once_b[8];
  sppc_pthread_once_init(once_b);
  sppc_pthread_once(once_b, bump_b);
  sppc_pthread_once(once_b, bump_b);
  snprintf(detail, sizeof detail, "a=%d b=%d", counter_a, counter_b);
  CHECK("separate handles are independent", counter_a == 1 && counter_b == 1, detail);

  // --- re-initialising the handle arms it again ---
  sppc_pthread_once_init(once_a);
  sppc_pthread_once(once_a, bump_a);
  snprintf(detail, sizeof detail, "ran %d time(s) total", counter_a);
  CHECK("re-init allows one more run", counter_a == 2, detail);

  // --- concurrent callers still see exactly one run ---
  sppc_pthread_once_init(shared_once);
  pthread_t th[THREADS];
  for (int i = 0; i < THREADS; i++) pthread_create(&th[i], NULL, hammer, NULL);
  for (int i = 0; i < THREADS; i++) pthread_join(th[i], NULL);
  snprintf(detail, sizeof detail, "%d threads x %d calls -> ran %d time(s)",
           THREADS, PER_THREAD, shared_counter);
  CHECK("exactly one run under contention", shared_counter == 1, detail);

  return TEST_SUMMARY();
}