#include "check.h"

// The Windows pthreads (platform/windows.h, PTHREADS) through their POSIX names,
// as the code shared with the other platforms calls them: threads and their
// keys, once, each kind of mutex, condition variables, rwlocks, spin locks
// and barriers between os threads, every "try" and timed form, the errors
// glibc gives for misuse, and a key's destructor on a thread sppc did not
// start. The public sppc_pthread_* api is tested by test_threads.c once the
// rest of the port runs it; this is what that will stand on.

#if !SPPC_PLATFORM_WINDOWS

int main(void) {
  printf("skipped: Windows only\n");
  return CHECK_SKIP_STATUS;
}

#else

// ==================== HELPERS ====================

static int64_t now_ms(const clockid_t clock) {
  struct timespec ts;
  clock_gettime(clock, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static struct timespec in_ms(const clockid_t clock, const long ms) {
  struct timespec ts;
  clock_gettime(clock, &ts);
  ts.tv_sec += ms / 1000;
  ts.tv_nsec += (ms % 1000) * 1000000L;
  if (ts.tv_nsec >= 1000000000L) {
    ts.tv_sec += 1;
    ts.tv_nsec -= 1000000000L;
  } else if (ts.tv_nsec < 0) { // a deadline already passed
    ts.tv_sec -= 1;
    ts.tv_nsec += 1000000000L;
  }
  return ts;
}

static pthread_t start(void *(*fn)(void*), void *arg) {
  pthread_t t = NULL;
  const int rc = pthread_create(&t, NULL, fn, arg);
  CHECK(rc == 0);
  return t;
}

static void finish(const pthread_t t) {
  CHECK(pthread_join(t, NULL) == 0);
}

/// Runs "fn" on its own thread and gives back what it returned, as an int:
/// for checking what a lock says to a thread other than its holder.
static int on_other_thread(void *(*fn)(void*), void *arg) {
  void *rc = NULL;
  CHECK(pthread_join(start(fn, arg), &rc) == 0);
  return (int)(intptr_t)rc;
}

/// Waits (up to two seconds) for a counter to reach a value, so a check that
/// depends on another thread having got somewhere fails rather than hangs.
static bool await_count(volatile LONG *counter, const LONG value) {
  for (int i = 0; i < 2000 && *counter != value; ++i) { Sleep(1); }
  return *counter == value;
}

// ==================== CLOCKS ====================

static void check_clocks(void) {
  struct timespec a, b;
  CHECK(clock_gettime(CLOCK_MONOTONIC, &a) == 0);
  CHECK(clock_gettime(CLOCK_MONOTONIC, &b) == 0);
  CHECK(b.tv_sec > a.tv_sec || (b.tv_sec == a.tv_sec && b.tv_nsec >= a.tv_nsec));
  CHECK(a.tv_nsec >= 0 && a.tv_nsec < 1000000000L);

  // Real time is the Unix epoch's, as "time" counts it.
  CHECK(clock_gettime(CLOCK_REALTIME, &a) == 0);
  const time_t t = time(NULL);
  CHECK(a.tv_sec >= t - 5 && a.tv_sec <= t + 5);
  CHECK(a.tv_nsec >= 0 && a.tv_nsec < 1000000000L);

  errno = 0;
  CHECK(clock_gettime(42, &a) == -1 && errno == EINVAL);

  // A monotonic sleep of 30 ms is seen as at least that.
  const int64_t before = now_ms(CLOCK_MONOTONIC);
  Sleep(30);
  CHECK(now_ms(CLOCK_MONOTONIC) - before >= 29);
}

// ==================== THREADS ====================

static pthread_t main_thread;
static pthread_t seen_self;

static void *return_arg(void *arg) {
  seen_self = pthread_self();
  return arg;
}

static HANDLE gate;
static volatile LONG gated_done = 0;

static void *wait_at_gate(void *arg) {
  (void)arg;
  WaitForSingleObject(gate, INFINITE);
  InterlockedIncrement(&gated_done);
  return NULL;
}

static void check_threads(void) {
  main_thread = pthread_self();
  CHECK(pthread_equal(main_thread, pthread_self()));

  // A thread's result comes back through the join, and it is not this one.
  void *rc = NULL;
  const pthread_t t = start(return_arg, (void*)(intptr_t)42);
  CHECK(pthread_join(t, &rc) == 0);
  CHECK((intptr_t)rc == 42);
  CHECK(!pthread_equal(seen_self, main_thread));

  // Joining yourself would never return.
  CHECK(pthread_join(pthread_self(), NULL) == EDEADLK);

  // A detached thread cannot then be joined, and still runs to its end.
  gate = CreateEventA(NULL, TRUE, FALSE, NULL);
  const pthread_t d = start(wait_at_gate, NULL);
  CHECK(pthread_detach(d) == 0);
  CHECK(pthread_join(d, NULL) == EINVAL);
  CHECK(pthread_detach(d) == EINVAL);
  SetEvent(gate);
  CHECK(await_count(&gated_done, 1));

  // So does one started detached, with a stack size of its own.
  pthread_attr_t attr;
  CHECK(pthread_attr_init(&attr) == 0);
  CHECK(pthread_attr_setdetachstate(&attr, 7) == EINVAL);
  CHECK(pthread_attr_setstacksize(&attr, 16) == EINVAL);
  CHECK(pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) == 0);
  CHECK(pthread_attr_setstacksize(&attr, 256 * 1024) == 0);
  pthread_t d2;
  CHECK(pthread_create(&d2, &attr, wait_at_gate, NULL) == 0);
  CHECK(pthread_attr_destroy(&attr) == 0);
  CHECK(await_count(&gated_done, 2));
  CloseHandle(gate);
}

// ==================== KEYS ====================

static pthread_key_t key;
static void *destroyed_with = NULL;
static volatile LONG destructor_calls = 0;
static int marker_a, marker_b;

static void record_destroy(void *value) {
  destroyed_with = value;
  InterlockedIncrement(&destructor_calls);
}

static void *set_marker(void *arg) {
  CHECK(pthread_getspecific(key) == NULL);
  CHECK(pthread_setspecific(key, arg) == 0);
  CHECK(pthread_getspecific(key) == arg);
  return NULL;
}

static DWORD WINAPI foreign_set_marker(LPVOID arg) {
  (void)pthread_self();
  set_marker(arg);
  return 0;
}

// Sets the value again from its own destructor, once: POSIX runs the
// destructors again for it, up to PTHREAD_DESTRUCTOR_ITERATIONS rounds.
static pthread_key_t again_key;
static volatile LONG again_calls = 0;

static void destroy_and_set_again(void *value) {
  if (InterlockedIncrement(&again_calls) == 1) { pthread_setspecific(again_key, value); }
}

static void *set_again(void *arg) {
  pthread_setspecific(again_key, arg);
  return NULL;
}

static void check_keys(void) {
  CHECK(pthread_key_create(&key, record_destroy) == 0);
  CHECK(pthread_getspecific(key) == NULL);
  CHECK(pthread_setspecific(key, &marker_a) == 0);

  // Each thread has its own value, handed to the destructor as it ends.
  finish(start(set_marker, &marker_b));
  CHECK(destructor_calls == 1 && destroyed_with == &marker_b);
  CHECK(pthread_getspecific(key) == &marker_a);

  // Including a thread sppc did not start: its exit comes through the
  // image's TLS callback, before its handle is signalled.
  const HANDLE h = CreateThread(NULL, 0, foreign_set_marker, &marker_b, 0, NULL);
  CHECK(h != NULL);
  WaitForSingleObject(h, INFINITE);
  CloseHandle(h);
  CHECK(destructor_calls == 2 && destroyed_with == &marker_b);

  // A value set by a destructor gets another round.
  CHECK(pthread_key_create(&again_key, destroy_and_set_again) == 0);
  finish(start(set_again, &marker_a));
  CHECK(again_calls == 2);
  CHECK(pthread_key_delete(again_key) == 0);

  // A deleted key's slot, made again, does not show the old key's value.
  const pthread_key_t old = key;
  CHECK(pthread_key_delete(key) == 0);
  CHECK(pthread_key_delete(key) == EINVAL);
  CHECK(pthread_setspecific(old, &marker_a) == EINVAL);
  CHECK(pthread_key_create(&key, NULL) == 0);
  CHECK(key == old);
  CHECK(pthread_getspecific(key) == NULL);
  CHECK(pthread_key_delete(key) == 0);
  CHECK(pthread_setspecific(PTHREAD_KEYS_MAX, &marker_a) == EINVAL);
  CHECK(pthread_getspecific(PTHREAD_KEYS_MAX) == NULL);

  // There are PTHREAD_KEYS_MAX of them, and then no more.
  pthread_key_t keys[PTHREAD_KEYS_MAX];
  int made = 0;
  while (made < PTHREAD_KEYS_MAX && pthread_key_create(&keys[made], NULL) == 0) { ++made; }
  CHECK(made == PTHREAD_KEYS_MAX);
  pthread_key_t extra;
  CHECK(pthread_key_create(&extra, NULL) == EAGAIN);
  for (int i = 0; i < made; ++i) { pthread_key_delete(keys[i]); }
}

// ==================== ONCE ====================

static pthread_once_t once = PTHREAD_ONCE_INIT;
static volatile LONG once_runs = 0;

static void once_body(void) {
  Sleep(10); // long enough for the others to arrive and wait
  InterlockedIncrement(&once_runs);
}

static void *call_once(void *arg) {
  (void)arg;
  CHECK(pthread_once(&once, once_body) == 0);
  CHECK(once_runs == 1); // nobody returns before it has run
  return NULL;
}

static void check_once(void) {
  _Static_assert(sizeof(pthread_once_t) <= sizeof(uint64_t), "a once must fit in an sppc handle");
  pthread_t t[8];
  for (int i = 0; i < 8; ++i) { t[i] = start(call_once, NULL); }
  for (int i = 0; i < 8; ++i) { finish(t[i]); }
  CHECK(once_runs == 1);
}

// ==================== MUTEXES ====================

static pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;
static long counter = 0;

static void *count_up(void *arg) {
  (void)arg;
  for (int i = 0; i < 100000; ++i) {
    pthread_mutex_lock(&counter_mutex);
    ++counter;
    pthread_mutex_unlock(&counter_mutex);
  }
  return NULL;
}

static void *trylock_it(void *mutex) {
  const int rc = pthread_mutex_trylock((pthread_mutex_t*)mutex);
  if (rc == 0) { pthread_mutex_unlock((pthread_mutex_t*)mutex); }
  return (void*)(intptr_t)rc;
}

static void *unlock_it(void *mutex) {
  return (void*)(intptr_t)pthread_mutex_unlock((pthread_mutex_t*)mutex);
}

static pthread_mutex_t held_mutex;
static HANDLE held, release;

static void *hold_mutex(void *arg) {
  (void)arg;
  pthread_mutex_lock(&held_mutex);
  SetEvent(held);
  WaitForSingleObject(release, INFINITE);
  pthread_mutex_unlock(&held_mutex);
  return NULL;
}

static pthread_mutex_t make_mutex(const int kind) {
  pthread_mutexattr_t attr;
  pthread_mutex_t m;
  int got = -1;
  CHECK(pthread_mutexattr_init(&attr) == 0);
  CHECK(pthread_mutexattr_settype(&attr, kind) == 0);
  CHECK(pthread_mutexattr_gettype(&attr, &got) == 0 && got == kind);
  CHECK(pthread_mutex_init(&m, &attr) == 0);
  CHECK(pthread_mutexattr_destroy(&attr) == 0);
  return m;
}

static void check_mutexes(void) {
  // Many threads, one counter: no increment lost.
  pthread_t t[8];
  for (int i = 0; i < 8; ++i) { t[i] = start(count_up, NULL); }
  for (int i = 0; i < 8; ++i) { finish(t[i]); }
  CHECK(counter == 800000);

  pthread_mutexattr_t attr;
  CHECK(pthread_mutexattr_init(&attr) == 0);
  CHECK(pthread_mutexattr_settype(&attr, 9) == EINVAL);

  // Normal: busy to everyone while held, this thread included, and not
  // destroyed while held.
  pthread_mutex_t normal = make_mutex(PTHREAD_MUTEX_NORMAL);
  CHECK(pthread_mutex_lock(&normal) == 0);
  CHECK(pthread_mutex_trylock(&normal) == EBUSY);
  CHECK(on_other_thread(trylock_it, &normal) == EBUSY);
  CHECK(pthread_mutex_destroy(&normal) == EBUSY);
  CHECK(pthread_mutex_unlock(&normal) == 0);
  CHECK(on_other_thread(trylock_it, &normal) == 0);
  CHECK(pthread_mutex_destroy(&normal) == 0);

  // Error-checking: a relock and a stranger's unlock are refused.
  pthread_mutex_t check = make_mutex(PTHREAD_MUTEX_ERRORCHECK);
  CHECK(pthread_mutex_lock(&check) == 0);
  CHECK(pthread_mutex_lock(&check) == EDEADLK);
  CHECK(pthread_mutex_trylock(&check) == EBUSY);
  CHECK(on_other_thread(unlock_it, &check) == EPERM);
  CHECK(pthread_mutex_unlock(&check) == 0);
  CHECK(pthread_mutex_unlock(&check) == EPERM);
  CHECK(pthread_mutex_destroy(&check) == 0);

  // Recursive: taken as often as released, by its holder only.
  pthread_mutex_t rec = make_mutex(PTHREAD_MUTEX_RECURSIVE);
  CHECK(pthread_mutex_lock(&rec) == 0);
  CHECK(pthread_mutex_lock(&rec) == 0);
  CHECK(pthread_mutex_trylock(&rec) == 0);
  CHECK(on_other_thread(trylock_it, &rec) == EBUSY);
  CHECK(on_other_thread(unlock_it, &rec) == EPERM);
  CHECK(pthread_mutex_unlock(&rec) == 0);
  CHECK(pthread_mutex_unlock(&rec) == 0);
  CHECK(on_other_thread(trylock_it, &rec) == EBUSY);
  CHECK(pthread_mutex_unlock(&rec) == 0);
  CHECK(on_other_thread(trylock_it, &rec) == 0);
  CHECK(pthread_mutex_unlock(&rec) == EPERM);
  CHECK(pthread_mutex_destroy(&rec) == 0);

  // Timed: held elsewhere, the wait runs out, and not early; on either clock.
  held = CreateEventA(NULL, TRUE, FALSE, NULL);
  release = CreateEventA(NULL, TRUE, FALSE, NULL);
  CHECK(pthread_mutex_init(&held_mutex, NULL) == 0);
  const pthread_t holder = start(hold_mutex, NULL);
  WaitForSingleObject(held, INFINITE);
  int64_t before = now_ms(CLOCK_MONOTONIC);
  struct timespec deadline = in_ms(CLOCK_MONOTONIC, 50);
  CHECK(_sppc_pthread_mutex_clocklock(&held_mutex, CLOCK_MONOTONIC, &deadline) == ETIMEDOUT);
  CHECK(now_ms(CLOCK_MONOTONIC) - before >= 50);
  deadline = in_ms(CLOCK_REALTIME, 30);
  before = now_ms(CLOCK_REALTIME);
  CHECK(pthread_mutex_timedlock(&held_mutex, &deadline) == ETIMEDOUT);
  CHECK(now_ms(CLOCK_REALTIME) - before >= 30);

  // A deadline already gone, a clock that cannot time a wait, and a time
  // that is not one.
  deadline = in_ms(CLOCK_MONOTONIC, -10);
  CHECK(_sppc_pthread_mutex_clocklock(&held_mutex, CLOCK_MONOTONIC, &deadline) == ETIMEDOUT);
  CHECK(_sppc_pthread_mutex_clocklock(&held_mutex, 42, &deadline) == EINVAL);
  deadline.tv_nsec = 1000000000L;
  CHECK(_sppc_pthread_mutex_clocklock(&held_mutex, CLOCK_MONOTONIC, &deadline) == EINVAL);

  // And a timed lock that gets it, once the holder lets go mid-wait.
  deadline = in_ms(CLOCK_MONOTONIC, 5000);
  SetEvent(release);
  CHECK(_sppc_pthread_mutex_clocklock(&held_mutex, CLOCK_MONOTONIC, &deadline) == 0);
  CHECK(pthread_mutex_unlock(&held_mutex) == 0);
  finish(holder);
  CHECK(pthread_mutex_destroy(&held_mutex) == 0);
  CloseHandle(held);
  CloseHandle(release);
}

// ==================== CONDITION VARIABLES ====================

static pthread_mutex_t cv_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static int cv_go = 0;
static volatile LONG cv_waiting = 0, cv_woken = 0;

static void *wait_for_go(void *arg) {
  (void)arg;
  pthread_mutex_lock(&cv_mutex);
  InterlockedIncrement(&cv_waiting);
  while (!cv_go) { pthread_cond_wait(&cv, &cv_mutex); }
  InterlockedIncrement(&cv_woken);
  pthread_mutex_unlock(&cv_mutex);
  return NULL;
}

/// Lets "n" waiters in, then opens the gate with "wake" (signal, once per
/// waiter, or broadcast).
static void release_waiters(const int n, const bool broadcast) {
  pthread_t t[4];
  cv_go = 0;
  cv_waiting = cv_woken = 0;
  for (int i = 0; i < n; ++i) { t[i] = start(wait_for_go, NULL); }
  CHECK(await_count(&cv_waiting, n));
  Sleep(10); // and into the wait itself
  pthread_mutex_lock(&cv_mutex);
  cv_go = 1;
  if (broadcast) { pthread_cond_broadcast(&cv); }
  else { for (int i = 0; i < n; ++i) { pthread_cond_signal(&cv); } }
  pthread_mutex_unlock(&cv_mutex);
  for (int i = 0; i < n; ++i) { finish(t[i]); }
  CHECK(cv_woken == n);
}

static void check_conditions(void) {
  release_waiters(1, false);
  release_waiters(4, false);
  release_waiters(4, true);

  // A signal nobody waits for is simply lost.
  CHECK(pthread_cond_signal(&cv) == 0);
  CHECK(pthread_cond_broadcast(&cv) == 0);

  // Timed waits run out, not early, with the mutex held again afterwards.
  pthread_mutex_lock(&cv_mutex);
  struct timespec deadline = in_ms(CLOCK_REALTIME, 30);
  int64_t before = now_ms(CLOCK_REALTIME);
  CHECK(pthread_cond_timedwait(&cv, &cv_mutex, &deadline) == ETIMEDOUT);
  CHECK(now_ms(CLOCK_REALTIME) - before >= 30);
  CHECK(on_other_thread(trylock_it, &cv_mutex) == EBUSY);
  deadline = in_ms(CLOCK_MONOTONIC, 30);
  before = now_ms(CLOCK_MONOTONIC);
  CHECK(_sppc_pthread_cond_clockwait(&cv, &cv_mutex, CLOCK_MONOTONIC, &deadline) == ETIMEDOUT);
  CHECK(now_ms(CLOCK_MONOTONIC) - before >= 30);
  CHECK(on_other_thread(trylock_it, &cv_mutex) == EBUSY);

  // Refused before the mutex is let go.
  CHECK(_sppc_pthread_cond_clockwait(&cv, &cv_mutex, 42, &deadline) == EINVAL);
  CHECK(on_other_thread(trylock_it, &cv_mutex) == EBUSY);
  pthread_mutex_unlock(&cv_mutex);

  // A wait on a mutex this thread does not hold (error-checking) is refused.
  pthread_mutex_t check = make_mutex(PTHREAD_MUTEX_ERRORCHECK);
  CHECK(pthread_cond_wait(&cv, &check) == EPERM);
  pthread_mutex_destroy(&check);

  // Its own clock, from the attributes, is what "timedwait" counts in.
  pthread_condattr_t attr;
  clockid_t clock = -1;
  pthread_cond_t mono;
  CHECK(pthread_condattr_init(&attr) == 0);
  CHECK(pthread_condattr_setclock(&attr, 42) == EINVAL);
  CHECK(pthread_condattr_setclock(&attr, CLOCK_MONOTONIC) == 0);
  CHECK(pthread_condattr_getclock(&attr, &clock) == 0 && clock == CLOCK_MONOTONIC);
  CHECK(pthread_cond_init(&mono, &attr) == 0);
  CHECK(pthread_condattr_destroy(&attr) == 0);
  pthread_mutex_lock(&cv_mutex);
  deadline = in_ms(CLOCK_MONOTONIC, 20);
  before = now_ms(CLOCK_MONOTONIC);
  CHECK(pthread_cond_timedwait(&mono, &cv_mutex, &deadline) == ETIMEDOUT);
  CHECK(now_ms(CLOCK_MONOTONIC) - before >= 20);
  pthread_mutex_unlock(&cv_mutex);
  CHECK(pthread_cond_destroy(&mono) == 0);
}

// ==================== RWLOCKS ====================

static pthread_rwlock_t rw = PTHREAD_RWLOCK_INITIALIZER;
static volatile LONG readers_in = 0;

static void *read_together(void *arg) {
  (void)arg;
  pthread_rwlock_rdlock(&rw);
  InterlockedIncrement(&readers_in);
  const bool met = await_count(&readers_in, 2); // both inside at once
  pthread_rwlock_unlock(&rw);
  return (void*)(intptr_t)met;
}

static void *try_read(void *arg) {
  (void)arg;
  const int rc = pthread_rwlock_tryrdlock(&rw);
  if (rc == 0) { pthread_rwlock_unlock(&rw); }
  return (void*)(intptr_t)rc;
}

static void *try_write(void *arg) {
  (void)arg;
  const int rc = pthread_rwlock_trywrlock(&rw);
  if (rc == 0) { pthread_rwlock_unlock(&rw); }
  return (void*)(intptr_t)rc;
}

static void *timed_read(void *arg) {
  (void)arg;
  const struct timespec deadline = in_ms(CLOCK_MONOTONIC, 30);
  return (void*)(intptr_t)_sppc_pthread_rwlock_clockrdlock(&rw, CLOCK_MONOTONIC, &deadline);
}

static void *timed_write(void *arg) {
  (void)arg;
  const struct timespec deadline = in_ms(CLOCK_REALTIME, 30);
  return (void*)(intptr_t)pthread_rwlock_timedwrlock(&rw, &deadline);
}

static void *unlock_rw(void *arg) {
  (void)arg;
  return (void*)(intptr_t)pthread_rwlock_unlock(&rw);
}

// Writers keep "pair" even outside the lock by adding 2 in two steps;
// readers must never see it odd.
static long pair = 0;
static volatile LONG odd_seen = 0;

static void *write_pairs(void *arg) {
  (void)arg;
  for (int i = 0; i < 20000; ++i) {
    pthread_rwlock_wrlock(&rw);
    ++pair;
    ++pair;
    pthread_rwlock_unlock(&rw);
  }
  return NULL;
}

static void *read_pairs(void *arg) {
  (void)arg;
  for (int i = 0; i < 20000; ++i) {
    pthread_rwlock_rdlock(&rw);
    if (pair % 2 != 0) { InterlockedIncrement(&odd_seen); }
    pthread_rwlock_unlock(&rw);
  }
  return NULL;
}

static void check_rwlocks(void) {
  // Readers share it.
  const pthread_t r1 = start(read_together, NULL), r2 = start(read_together, NULL);
  void *met1 = NULL, *met2 = NULL;
  CHECK(pthread_join(r1, &met1) == 0 && pthread_join(r2, &met2) == 0);
  CHECK(met1 && met2);

  // A writer has it alone, and is refused it again and a read lock besides.
  CHECK(pthread_rwlock_wrlock(&rw) == 0);
  CHECK(on_other_thread(try_read, NULL) == EBUSY);
  CHECK(on_other_thread(try_write, NULL) == EBUSY);
  CHECK(on_other_thread(timed_read, NULL) == ETIMEDOUT);
  CHECK(pthread_rwlock_wrlock(&rw) == EDEADLK);
  CHECK(pthread_rwlock_rdlock(&rw) == EDEADLK);
  CHECK(pthread_rwlock_trywrlock(&rw) == EBUSY);
  CHECK(on_other_thread(unlock_rw, NULL) == EPERM);
  CHECK(pthread_rwlock_destroy(&rw) == EBUSY);
  CHECK(pthread_rwlock_unlock(&rw) == 0);
  CHECK(pthread_rwlock_unlock(&rw) == EPERM);

  // A reader keeps writers out, not other readers.
  CHECK(pthread_rwlock_rdlock(&rw) == 0);
  CHECK(pthread_rwlock_tryrdlock(&rw) == 0);
  CHECK(on_other_thread(try_read, NULL) == 0);
  CHECK(on_other_thread(try_write, NULL) == EBUSY);
  CHECK(on_other_thread(timed_write, NULL) == ETIMEDOUT);
  CHECK(pthread_rwlock_unlock(&rw) == 0);
  CHECK(pthread_rwlock_unlock(&rw) == 0);
  CHECK(on_other_thread(try_write, NULL) == 0);

  const struct timespec bad = {0, -1};
  CHECK(_sppc_pthread_rwlock_clockwrlock(&rw, CLOCK_MONOTONIC, &bad) == EINVAL);
  CHECK(_sppc_pthread_rwlock_clockrdlock(&rw, 42, &bad) == EINVAL);

  // Under load, no reader sees a writer half-way.
  pthread_t t[8];
  for (int i = 0; i < 8; ++i) { t[i] = start(i % 2 ? write_pairs : read_pairs, NULL); }
  for (int i = 0; i < 8; ++i) { finish(t[i]); }
  CHECK(odd_seen == 0);
  CHECK(pair == 4 * 20000 * 2);
  CHECK(pthread_rwlock_destroy(&rw) == 0);
}

// ==================== SPIN LOCKS ====================

static pthread_spinlock_t spin;
static long spun = 0;

static void *spin_up(void *arg) {
  (void)arg;
  for (int i = 0; i < 100000; ++i) {
    pthread_spin_lock(&spin);
    ++spun;
    pthread_spin_unlock(&spin);
  }
  return NULL;
}

static void *try_spin(void *arg) {
  (void)arg;
  const int rc = pthread_spin_trylock(&spin);
  if (rc == 0) { pthread_spin_unlock(&spin); }
  return (void*)(intptr_t)rc;
}

static void check_spin(void) {
  _Static_assert(sizeof(pthread_spinlock_t) <= sizeof(uint64_t), "a spin lock must fit in an sppc handle");
  CHECK(pthread_spin_init(&spin, 0) == 0);
  pthread_t t[4];
  for (int i = 0; i < 4; ++i) { t[i] = start(spin_up, NULL); }
  for (int i = 0; i < 4; ++i) { finish(t[i]); }
  CHECK(spun == 400000);
  CHECK(pthread_spin_lock(&spin) == 0);
  CHECK(pthread_spin_trylock(&spin) == EBUSY);
  CHECK(on_other_thread(try_spin, NULL) == EBUSY);
  CHECK(pthread_spin_unlock(&spin) == 0);
  CHECK(on_other_thread(try_spin, NULL) == 0);
  CHECK(pthread_spin_destroy(&spin) == 0);
}

// ==================== BARRIERS ====================

#define ROUNDS 50
#define PARTIES 4

static pthread_barrier_t barrier;
static volatile LONG arrived[ROUNDS];
static volatile LONG serial = 0, short_rounds = 0;

static void *meet(void *arg) {
  (void)arg;
  for (int round = 0; round < ROUNDS; ++round) {
    InterlockedIncrement(&arrived[round]);
    const int rc = pthread_barrier_wait(&barrier);
    if (rc == PTHREAD_BARRIER_SERIAL_THREAD) { InterlockedIncrement(&serial); }
    else { CHECK(rc == 0); }
    if (arrived[round] != PARTIES) { InterlockedIncrement(&short_rounds); }
  }
  return NULL;
}

static void *wait_at_barrier(void *arg) {
  (void)arg;
  return (void*)(intptr_t)pthread_barrier_wait(&barrier);
}

static void check_barrier(void) {
  CHECK(pthread_barrier_init(&barrier, NULL, 0) == EINVAL);
  CHECK(pthread_barrier_init(&barrier, NULL, PARTIES) == 0);
  pthread_t t[PARTIES];
  for (int i = 0; i < PARTIES; ++i) { t[i] = start(meet, NULL); }
  for (int i = 0; i < PARTIES; ++i) { finish(t[i]); }

  // Nobody passes before everyone arrives, and one per round is the serial one.
  CHECK(short_rounds == 0);
  CHECK(serial == ROUNDS);
  CHECK(pthread_barrier_destroy(&barrier) == 0);

  // Destroying it part-way through a round is refused; the last to arrive
  // is the serial one.
  CHECK(pthread_barrier_init(&barrier, NULL, 2) == 0);
  const pthread_t waiter = start(wait_at_barrier, NULL);
  Sleep(20);
  CHECK(pthread_barrier_destroy(&barrier) == EBUSY);
  CHECK(pthread_barrier_wait(&barrier) == PTHREAD_BARRIER_SERIAL_THREAD);
  void *rc = NULL;
  CHECK(pthread_join(waiter, &rc) == 0 && (intptr_t)rc == 0);
  CHECK(pthread_barrier_destroy(&barrier) == 0);
}

int main(void) {
  check_clocks();
  check_threads();
  check_keys();
  check_once();
  check_mutexes();
  check_conditions();
  check_rwlocks();
  check_spin();
  check_barrier();
  return check_done();
}

#endif
