#include "check.h"

// The async runtime on Windows fibers (async/context.h): what the shared
// scheduling tests cannot see there. Each thread that uses the runtime
// becomes a fiber and is made a thread again at its exit, with every task
// fiber deleted, whether sppc started the thread or not, and over many
// threads nothing is left behind. A thread its host had already made a
// fiber is used as it is and left one. Each task keeps its own
// floating-point state across a switch.

#if !SPPC_PLATFORM_WINDOWS

int main(void) {
  printf("skipped: Windows only\n");
  return CHECK_SKIP_STATUS;
}

#else

#include <fenv.h>

// ==================== HELPERS ====================

/// Bytes of address space reserved or committed in this process: a leaked
/// fiber leaves its whole stack reservation here.
static size_t mapped_bytes(void) {
  size_t total = 0;
  MEMORY_BASIC_INFORMATION mbi;
  for (char *p = NULL; VirtualQuery(p, &mbi, sizeof mbi) == sizeof mbi; p = (char*)mbi.BaseAddress + mbi.RegionSize) {
    if (mbi.State != MEM_FREE) { total += mbi.RegionSize; }
  }
  return total;
}

static DWORD handles(void) {
  DWORD n = 0;
  GetProcessHandleCount(GetCurrentProcess(), &n);
  return n;
}

// The runtime frees a task's environment when it ends, so it is the
// heap's, laid out as every closure's is, and holds where the count is.
typedef struct { sppc_closure_env head; long *count; } count_env;

static void add_one(void *e) {
  ++*((count_env*)e)->count;
}

static void add_one_yielding(void *e) {
  sppc_async_yield();
  ++*((count_env*)e)->count;
}

/// A few tasks on the calling thread, one of them yielding, all awaited:
/// enough for the runtime to start, make fibers, reuse one and cache some.
static long run_tasks(void) {
  long count = 0;
  int h[4];
  for (int i = 0; i < 4; ++i) {
    count_env *const env = (count_env*)sppc_malloc(sizeof *env);
    env->head = (sppc_closure_env){1, NULL};
    env->count = &count; // on this stack, which every task finishes before
    CHECK(sppc_async(&h[i], (sppc_closure){i % 2 ? add_one_yielding : add_one, env}) == 0);
  }
  for (int i = 0; i < 4; ++i) { CHECK(sppc_await(h[i]) == 0); }
  return count;
}

static volatile LONG tasks_ok = 0;

static void *pthread_body(void *arg) {
  (void)arg;
  if (run_tasks() == 4 && IsThreadAFiber()) { InterlockedIncrement(&tasks_ok); }
  return NULL;
}

static DWORD WINAPI foreign_body(LPVOID arg) {
  (void)arg;
  if (run_tasks() == 4 && IsThreadAFiber()) { InterlockedIncrement(&tasks_ok); }
  return 0;
}

#define THREADS 64

static void round_of_pthreads(void) {
  pthread_t t[THREADS];
  for (int i = 0; i < THREADS; ++i) { CHECK(pthread_create(&t[i], NULL, pthread_body, NULL) == 0); }
  for (int i = 0; i < THREADS; ++i) { CHECK(pthread_join(t[i], NULL) == 0); }
}

static void round_of_foreign_threads(void) {
  HANDLE t[THREADS];
  for (int i = 0; i < THREADS; ++i) { t[i] = CreateThread(NULL, 0, foreign_body, NULL, 0, NULL); }
  for (int i = 0; i < THREADS; ++i) {
    WaitForSingleObject(t[i], INFINITE);
    CloseHandle(t[i]);
  }
}

// ==================== TEARDOWN ====================

/// Rounds of threads that each start the runtime and end: after the first
/// (which warms the heap and the loader up), more rounds add nothing, so no
/// fiber, stack or handle outlives its thread.
static void check_no_leak(char const *what, void (*round)(void)) {
  tasks_ok = 0;
  round();
  const size_t mapped = mapped_bytes();
  const DWORD open = handles();
  for (int i = 0; i < 4; ++i) { round(); }
  CHECK(tasks_ok == 5 * THREADS);
  const size_t grown = mapped_bytes() > mapped ? mapped_bytes() - mapped : 0;
  const long more_handles = (long)handles() - (long)open;
  printf("%s: %d threads, %zu KiB more mapped, %ld more handles\n", what, 4 * THREADS, grown / 1024, more_handles);
  // One leaked fiber per thread would be 256 reservations of 128 KiB and up.
  CHECK(grown < 4u * 1024 * 1024);
  CHECK(more_handles < 16);
}

// ==================== A HOST'S FIBER ====================

static void *host_fiber;
static volatile LONG host_ok = 0;

static DWORD WINAPI host_fiber_body(LPVOID arg) {
  (void)arg;
  host_fiber = ConvertThreadToFiber(NULL);
  if (host_fiber == NULL) { return 1; }
  // The runtime runs on the fiber it finds, and leaves the thread one: the
  // host made it so, and is the one to undo it.
  if (run_tasks() == 4 && _gt_current_fiber() == host_fiber) { InterlockedIncrement(&host_ok); }
  _gt_drain(); // this thread's runtime; "sppc_cleanup" is the process's, once per "sppc_init"
  if (IsThreadAFiber() && _gt_current_fiber() == host_fiber) { InterlockedIncrement(&host_ok); }
  if (ConvertFiberToThread()) { InterlockedIncrement(&host_ok); }
  return 0;
}

static void check_host_fiber(void) {
  const HANDLE t = CreateThread(NULL, 0, host_fiber_body, NULL, 0, NULL);
  WaitForSingleObject(t, INFINITE);
  DWORD code = 99;
  GetExitCodeThread(t, &code);
  CloseHandle(t);
  CHECK(code == 0);
  CHECK(host_ok == 3);
}

// ==================== FLOATING POINT ====================

static int rounding_seen = -1;

/// Changes its rounding mode, lets the other task run, and finds it kept.
static void round_upward(void *e) {
  (void)e;
  CHECK(fesetround(FE_UPWARD) == 0);
  sppc_async_yield();
  CHECK(fegetround() == FE_UPWARD);
  fesetround(FE_TONEAREST);
}

/// Runs while the other is switched away mid-change, and sees its own.
static void read_rounding(void *e) {
  (void)e;
  rounding_seen = fegetround();
}

static void check_float_state(void) {
  int a = 0, b = 0;
  CHECK(sppc_async(&a, (sppc_closure){round_upward, NULL}) == 0);
  CHECK(sppc_async(&b, (sppc_closure){read_rounding, NULL}) == 0);
  CHECK(sppc_await(a) == 0 && sppc_await(b) == 0);
  CHECK(rounding_seen == FE_TONEAREST);
  CHECK(fegetround() == FE_TONEAREST);
}

int main(void) {
  CHECK(sppc_init() == 0);
  CHECK(IsThreadAFiber()); // the main thread too, once the runtime starts on it
  CHECK(run_tasks() == 4);
  check_float_state();
  check_no_leak("pthreads", round_of_pthreads);
  check_no_leak("threads sppc did not start", round_of_foreign_threads);
  check_host_fiber();
  CHECK(sppc_cleanup() == 0);
  return check_done();
}

#endif
