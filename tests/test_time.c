#include "check.h"

// Clocks and sleeps, on every platform: each of the four POSIX clocks reads,
// the cpu clocks count work done and not sleep, and a sleep, relative or to a
// deadline on either clock, never ends early. On Windows, where the clocks
// are sppc's own, also what is settled there: Linux's own clocks and sleeps
// on a cpu clock are refused, and a sleep is not rounded up to the
// scheduler's 15.6 ms tick.

#include <time.h>

static int64_t ms_on(const int clock) {
  sppc_timespec t = {0, 0};
  sppc_clock_gettime(clock, &t);
  return (int64_t)t.sec * 1000 + (int64_t)(t.nsec / 1000000);
}

static int64_t us_on(const int clock) {
  sppc_timespec t = {0, 0};
  sppc_clock_gettime(clock, &t);
  return (int64_t)t.sec * 1000000 + (int64_t)(t.nsec / 1000);
}

static sppc_timespec in_ms(const int clock, const uint64_t ms) {
  sppc_timespec t = {0, 0};
  sppc_clock_gettime(clock, &t);
  t.sec += ms / 1000;
  t.nsec += (ms % 1000) * 1000000;
  if (t.nsec >= 1000000000u) {
    t.sec += 1;
    t.nsec -= 1000000000u;
  }
  return t;
}

static void check_clocks(void) {
  int const clocks[] = {SPPC_CLOCK_REALTIME, SPPC_CLOCK_MONOTONIC, SPPC_CLOCK_PROCESS_CPUTIME_ID,
    SPPC_CLOCK_THREAD_CPUTIME_ID};
  for (size_t i = 0; i < sizeof clocks / sizeof *clocks; ++i) {
    sppc_timespec t = {7, 7};
    CHECK(sppc_clock_gettime(clocks[i], &t) == 0);
    CHECK(t.nsec < 1000000000u);
  }

  // Real time is the Unix epoch's.
  sppc_timespec now = {0, 0};
  CHECK(sppc_clock_gettime(SPPC_CLOCK_REALTIME, &now) == 0);
  CHECK(now.sec + 5 >= (uint64_t)time(NULL) && now.sec <= (uint64_t)time(NULL) + 5);

  // Monotonic never goes back.
  int64_t last = us_on(SPPC_CLOCK_MONOTONIC);
  bool forward = true;
  for (int i = 0; i < 10000; ++i) {
    const int64_t t = us_on(SPPC_CLOCK_MONOTONIC);
    forward = forward && t >= last;
    last = t;
  }
  CHECK(forward);

  // A clock that is none, and on Windows, Linux's own.
  sppc_timespec t;
  CHECK(sppc_clock_gettime(99, &t) == SPPC_EINVAL);
#if SPPC_PLATFORM_WINDOWS
  CHECK(sppc_clock_gettime(SPPC_CLOCK_MONOTONIC_RAW, &t) == SPPC_EINVAL);
  CHECK(sppc_clock_gettime(SPPC_CLOCK_REALTIME_COARSE, &t) == SPPC_EINVAL);
  CHECK(sppc_clock_gettime(SPPC_CLOCK_MONOTONIC_COARSE, &t) == SPPC_EINVAL);
  CHECK(sppc_clock_gettime(SPPC_CLOCK_BOOTTIME, &t) == SPPC_EINVAL);
#endif
}

static void check_cpu_clocks(void) {
  // Work moves the thread's clock (by scheduler ticks), the process's at
  // least as far; a sleep moves neither much.
  // Spun until it has (rather than for a wall-clock time, which a busy
  // machine may not give this thread much of), with a cap that only a
  // clock that does not count work reaches.
  const int64_t thread_before = ms_on(SPPC_CLOCK_THREAD_CPUTIME_ID);
  const int64_t wall_before = ms_on(SPPC_CLOCK_MONOTONIC);
  volatile uint64_t sink = 0;
  while (ms_on(SPPC_CLOCK_THREAD_CPUTIME_ID) - thread_before < 50 && ms_on(SPPC_CLOCK_MONOTONIC) - wall_before < 5000) {
    for (int i = 0; i < 100000; ++i) { sink += (uint64_t)i; }
  }
  (void)sink;
  CHECK(ms_on(SPPC_CLOCK_THREAD_CPUTIME_ID) - thread_before >= 50);
  CHECK(ms_on(SPPC_CLOCK_PROCESS_CPUTIME_ID) >= ms_on(SPPC_CLOCK_THREAD_CPUTIME_ID));

  const int64_t idle_before = ms_on(SPPC_CLOCK_THREAD_CPUTIME_ID);
  const sppc_timespec d = {0, 100000000};
  CHECK(sppc_clock_nanosleep(SPPC_CLOCK_MONOTONIC, 0, &d) == 0);
  CHECK(ms_on(SPPC_CLOCK_THREAD_CPUTIME_ID) - idle_before < 40);
}

static void check_sleeps(void) {
  // A relative sleep is at least as long as asked, on either clock.
  sppc_timespec d = {0, 30000000};
  int64_t before = us_on(SPPC_CLOCK_MONOTONIC);
  CHECK(sppc_clock_nanosleep(SPPC_CLOCK_MONOTONIC, 0, &d) == 0);
  CHECK(us_on(SPPC_CLOCK_MONOTONIC) - before >= 30000);
  before = us_on(SPPC_CLOCK_MONOTONIC);
  CHECK(sppc_clock_nanosleep(SPPC_CLOCK_REALTIME, 0, &d) == 0);
  CHECK(us_on(SPPC_CLOCK_MONOTONIC) - before >= 30000);

  // An absolute one wakes no earlier than its deadline, on its own clock.
  sppc_timespec deadline = in_ms(SPPC_CLOCK_MONOTONIC, 40);
  CHECK(sppc_clock_nanosleep(SPPC_CLOCK_MONOTONIC, SPPC_TIMER_ABSTIME, &deadline) == 0);
  sppc_timespec now = {0, 0};
  sppc_clock_gettime(SPPC_CLOCK_MONOTONIC, &now);
  CHECK(now.sec > deadline.sec || (now.sec == deadline.sec && now.nsec >= deadline.nsec));
  deadline = in_ms(SPPC_CLOCK_REALTIME, 40);
  CHECK(sppc_clock_nanosleep(SPPC_CLOCK_REALTIME, SPPC_TIMER_ABSTIME, &deadline) == 0);
  sppc_clock_gettime(SPPC_CLOCK_REALTIME, &now);
  CHECK(now.sec > deadline.sec || (now.sec == deadline.sec && now.nsec >= deadline.nsec));

  // A deadline already gone returns at once; so does a zero sleep.
  const sppc_timespec past = {1, 0};
  before = us_on(SPPC_CLOCK_MONOTONIC);
  CHECK(sppc_clock_nanosleep(SPPC_CLOCK_MONOTONIC, SPPC_TIMER_ABSTIME, &past) == 0);
  const sppc_timespec zero = {0, 0};
  CHECK(sppc_clock_nanosleep(SPPC_CLOCK_MONOTONIC, 0, &zero) == 0);
  CHECK(us_on(SPPC_CLOCK_MONOTONIC) - before < 100000);

#if SPPC_PLATFORM_WINDOWS
  // Not rounded to the scheduler's tick: the best of a few 2 ms sleeps is
  // well under 15.6 ms.
  int64_t best = INT64_MAX;
  d = (sppc_timespec){0, 2000000};
  for (int i = 0; i < 5; ++i) {
    before = us_on(SPPC_CLOCK_MONOTONIC);
    sppc_clock_nanosleep(SPPC_CLOCK_MONOTONIC, 0, &d);
    const int64_t took = us_on(SPPC_CLOCK_MONOTONIC) - before;
    if (took < best) { best = took; }
  }
  printf("2 ms sleep: best %lld us\n", (long long)best);
  CHECK(best >= 2000 && best < 10000);

  // Refused there: a cpu clock (it does not move while its thread sleeps),
  // and Linux's own.
  d = (sppc_timespec){0, 1000};
  CHECK(sppc_clock_nanosleep(SPPC_CLOCK_THREAD_CPUTIME_ID, 0, &d) == SPPC_EINVAL);
  CHECK(sppc_clock_nanosleep(SPPC_CLOCK_PROCESS_CPUTIME_ID, 0, &d) == SPPC_EINVAL);
  CHECK(sppc_clock_nanosleep(SPPC_CLOCK_BOOTTIME, 0, &d) == SPPC_EINVAL);
#endif

  // Refused everywhere: a flag that is not one, and a time that is not one.
  d = (sppc_timespec){0, 1000};
  CHECK(sppc_clock_nanosleep(SPPC_CLOCK_MONOTONIC, 4, &d) == SPPC_EINVAL);
  d = (sppc_timespec){0, 1000000000u};
  CHECK(sppc_clock_nanosleep(SPPC_CLOCK_MONOTONIC, 0, &d) == SPPC_EINVAL);
}

int main(void) {
  if (sppc_init() != 0) { return 1; }
  check_clocks();
  check_cpu_clocks();
  check_sleeps();
  sppc_cleanup();
  return check_done();
}
