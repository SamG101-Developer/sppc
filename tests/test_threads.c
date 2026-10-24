#include "check.h"

// Threads and their synchronisation, between real os threads: creation,
// joining and detaching, once, mutexes (plain and recursive), condition
// variables (including timed waits), rwlocks, spin locks and barriers, and
// the "try" form of each lock. Then the same locks taken by a task of the
// async runtime while another thread holds them.

static void sleep_ms(const uint64_t ms) {
  const sppc_timespec d = {0, ms * 1000000};
  sppc_clock_nanosleep(SPPC_CLOCK_MONOTONIC, 0, &d);
}

/// Start a thread running "fn" with a copy of "arg" (the closure's
/// environment is the thread's, freed when it ends).
static int spawn(uint64_t *handle, void (*fn)(void*), void const *arg, size_t arg_size) {
  void *env = NULL;
  if (arg_size > 0) {
    env = sppc_malloc(arg_size);
    memcpy(env, arg, arg_size);
  }
  return sppc_pthread_create((sppc_closure){fn, env}, handle);
}

// ==================== THREADS ====================

static uint64_t main_handle;
static _Atomic int ran = 0;
static uint64_t seen_self;
static int seen_equal_main = -1;
static void *seen_usp;

static void record_self(void *e) {
  ran += *(int*)e;
  sppc_pthread_self(&seen_self);
  uint64_t eq = 2;
  sppc_pthread_equal(&seen_self, &main_handle, &eq);
  seen_equal_main = (int)eq;
  seen_usp = __safestack_unsafe_stack_ptr;
}

static void check_create_join(void) {
  sppc_pthread_self(&main_handle);
  uint64_t again = 0, eq = 2;
  sppc_pthread_self(&again);
  sppc_pthread_equal(&main_handle, &again, &eq);
  CHECK(eq == 1);

  // A new thread is not this one, and runs on an unsafe stack of its own
  // once this one has asked for them.
  CHECK(sppc_unsafe_stack_up() == 0);
  void *const main_usp = __safestack_unsafe_stack_ptr;
  uint64_t t = 0;
  const int add = 5;
  CHECK(spawn(&t, record_self, &add, sizeof add) == 0);
  CHECK(sppc_pthread_join(&t) == 0);
  CHECK(ran == 5);
  CHECK(seen_equal_main == 0 && seen_self != main_handle);
  CHECK(seen_usp != NULL && seen_usp != main_usp);

  // A second "up" is nothing; "down" drops this thread's, and "up" brings it back.
  CHECK(sppc_unsafe_stack_up() == 0 && __safestack_unsafe_stack_ptr == main_usp);
  sppc_unsafe_stack_down();
  CHECK(__safestack_unsafe_stack_ptr == NULL);
  sppc_unsafe_stack_down();
  CHECK(sppc_unsafe_stack_up() == 0 && __safestack_unsafe_stack_ptr != NULL);

  // Joining yourself would never return, and is refused.
  CHECK(sppc_pthread_join(&main_handle) == SPPC_EDEADLK);
}

static uint64_t done_mutex, done_cond;
static int detached_done = 0;

static void detached_body(void *e) {
  (void)e;
  sleep_ms(5);
  sppc_pthread_mutex_lock(&done_mutex);
  detached_done = 1;
  sppc_pthread_cond_signal(&done_cond);
  sppc_pthread_mutex_unlock(&done_mutex);
}

static void check_detach(void) {
  CHECK(sppc_pthread_mutex_init(&done_mutex) == 0 && sppc_pthread_cond_init(&done_cond) == 0);
  uint64_t t = 0;
  CHECK(spawn(&t, detached_body, NULL, 0) == 0);
  CHECK(sppc_pthread_detach(&t) == 0);
  // Its end is seen through the condition variable, since it cannot be joined.
  CHECK(sppc_pthread_mutex_lock(&done_mutex) == 0);
  while (!detached_done) { CHECK(sppc_pthread_cond_wait(&done_cond, &done_mutex) == 0); }
  CHECK(sppc_pthread_mutex_unlock(&done_mutex) == 0);
  CHECK(sppc_pthread_cond_destroy(&done_cond) == 0 && sppc_pthread_mutex_destroy(&done_mutex) == 0);
}

// ==================== ONCE ====================

static uint64_t once;
static _Atomic int once_runs = 0;
static uint64_t once_start;

/// A closure's environment starts with its header ("sppc_closure_env"):
/// the runtime gives up the closures of the callers that lose the race
/// through it.
typedef struct {
  sppc_closure_env head;
  int one;
} once_env;

static void once_body(void *e) {
  once_runs += ((once_env*)e)->one;
}

static sppc_closure once_closure(void) {
  once_env *const env = sppc_malloc(sizeof *env);
  *env = (once_env){{1, NULL}, 1};
  return (sppc_closure){once_body, env};
}

static void call_once(void *e) {
  (void)e;
  sppc_pthread_barrier_wait(&once_start); // all at once, to race for it
  sppc_pthread_once(&once, once_closure());
}

static void check_once(void) {
  CHECK(sppc_pthread_once_init(&once) == 0);
  CHECK(sppc_pthread_barrier_init(&once_start, 4) == 0);
  uint64_t t[4];
  for (int i = 0; i < 4; ++i) { CHECK(spawn(&t[i], call_once, NULL, 0) == 0); }
  for (int i = 0; i < 4; ++i) { CHECK(sppc_pthread_join(&t[i]) == 0); }
  CHECK(once_runs == 1);
  // And never again, from this thread either.
  CHECK(sppc_pthread_once(&once, once_closure()) == 0 && once_runs == 1);
  CHECK(sppc_pthread_barrier_destroy(&once_start) == 0);
}

// ==================== MUTEXES ====================

static uint64_t counter_mutex;
static long counter = 0;

static void count_up(void *e) {
  (void)e;
  for (int i = 0; i < 20000; ++i) {
    sppc_pthread_mutex_lock(&counter_mutex);
    ++counter;
    sppc_pthread_mutex_unlock(&counter_mutex);
  }
}

static int trylock_rc = -1;
static void try_mutex(void *e) {
  (void)e;
  trylock_rc = sppc_pthread_mutex_trylock(&counter_mutex);
  if (trylock_rc == 0) { sppc_pthread_mutex_unlock(&counter_mutex); }
}

static void check_mutexes(void) {
  CHECK(sppc_pthread_mutex_init(&counter_mutex) == 0);
  uint64_t t[4];
  for (int i = 0; i < 4; ++i) { CHECK(spawn(&t[i], count_up, NULL, 0) == 0); }
  for (int i = 0; i < 4; ++i) { CHECK(sppc_pthread_join(&t[i]) == 0); }
  CHECK(counter == 4 * 20000);

  // trylock from another thread: refused while held here, granted once not.
  uint64_t th;
  CHECK(sppc_pthread_mutex_lock(&counter_mutex) == 0);
  CHECK(spawn(&th, try_mutex, NULL, 0) == 0 && sppc_pthread_join(&th) == 0);
  CHECK(trylock_rc == SPPC_EBUSY);
  CHECK(sppc_pthread_mutex_unlock(&counter_mutex) == 0);
  CHECK(spawn(&th, try_mutex, NULL, 0) == 0 && sppc_pthread_join(&th) == 0);
  CHECK(trylock_rc == 0);
  CHECK(sppc_pthread_mutex_destroy(&counter_mutex) == 0);

  // A recursive mutex can be taken again by its holder, and needs as many
  // unlocks as locks before another thread gets it.
  CHECK(sppc_pthread_mutex_init_recursive(&counter_mutex) == 0);
  CHECK(sppc_pthread_mutex_lock(&counter_mutex) == 0);
  CHECK(sppc_pthread_mutex_lock(&counter_mutex) == 0);
  CHECK(sppc_pthread_mutex_trylock(&counter_mutex) == 0);
  CHECK(sppc_pthread_mutex_unlock(&counter_mutex) == 0);
  CHECK(sppc_pthread_mutex_unlock(&counter_mutex) == 0);
  CHECK(spawn(&th, try_mutex, NULL, 0) == 0 && sppc_pthread_join(&th) == 0);
  CHECK(trylock_rc == SPPC_EBUSY);
  CHECK(sppc_pthread_mutex_unlock(&counter_mutex) == 0);
  CHECK(spawn(&th, try_mutex, NULL, 0) == 0 && sppc_pthread_join(&th) == 0);
  CHECK(trylock_rc == 0);
  CHECK(sppc_pthread_mutex_destroy(&counter_mutex) == 0);
}

// ==================== CONDITION VARIABLES ====================

static uint64_t cv_mutex, cv;
static int cv_go = 0;
static _Atomic int cv_woken = 0;

static void wait_for_go(void *e) {
  (void)e;
  sppc_pthread_mutex_lock(&cv_mutex);
  while (!cv_go) { sppc_pthread_cond_wait(&cv, &cv_mutex); }
  sppc_pthread_mutex_unlock(&cv_mutex);
  ++cv_woken;
}

static void signal_go(void *e) {
  (void)e;
  sleep_ms(10);
  sppc_pthread_mutex_lock(&cv_mutex);
  cv_go = 1;
  sppc_pthread_cond_signal(&cv);
  sppc_pthread_mutex_unlock(&cv_mutex);
}

static void check_conditions(void) {
  CHECK(sppc_pthread_mutex_init(&cv_mutex) == 0 && sppc_pthread_cond_init(&cv) == 0);

  // A timed wait with nobody signalling runs out.
  const sppc_timespec short_wait = {0, 20000000};
  CHECK(sppc_pthread_mutex_lock(&cv_mutex) == 0);
  CHECK(sppc_pthread_cond_clockwait(&cv, &cv_mutex, SPPC_CLOCK_MONOTONIC, &short_wait) == SPPC_ETIMEDOUT);
  CHECK(sppc_pthread_cond_clockwait(&cv, &cv_mutex, SPPC_CLOCK_REALTIME, &short_wait) == SPPC_ETIMEDOUT);
  CHECK(sppc_pthread_cond_clockwait(&cv, &cv_mutex, 99, &short_wait) == SPPC_EINVAL);
  CHECK(sppc_pthread_mutex_unlock(&cv_mutex) == 0);

  // A broadcast wakes every waiter.
  uint64_t t[3];
  for (int i = 0; i < 3; ++i) { CHECK(spawn(&t[i], wait_for_go, NULL, 0) == 0); }
  sleep_ms(20);
  CHECK(cv_woken == 0);
  CHECK(sppc_pthread_mutex_lock(&cv_mutex) == 0);
  cv_go = 1;
  CHECK(sppc_pthread_cond_broadcast(&cv) == 0);
  CHECK(sppc_pthread_mutex_unlock(&cv_mutex) == 0);
  for (int i = 0; i < 3; ++i) { CHECK(sppc_pthread_join(&t[i]) == 0); }
  CHECK(cv_woken == 3);

  // A signal wakes one; each waiter needs one of its own.
  cv_go = 0;
  cv_woken = 0;
  for (int i = 0; i < 2; ++i) { CHECK(spawn(&t[i], wait_for_go, NULL, 0) == 0); }
  sleep_ms(20);
  CHECK(sppc_pthread_mutex_lock(&cv_mutex) == 0);
  cv_go = 1;
  CHECK(sppc_pthread_cond_signal(&cv) == 0);
  CHECK(sppc_pthread_cond_signal(&cv) == 0);
  CHECK(sppc_pthread_mutex_unlock(&cv_mutex) == 0);
  for (int i = 0; i < 2; ++i) { CHECK(sppc_pthread_join(&t[i]) == 0); }
  CHECK(cv_woken == 2);

  // A timed wait that is signalled in time returns 0, long before its deadline.
  cv_go = 0;
  const sppc_timespec long_wait = {5, 0};
  CHECK(spawn(&t[0], signal_go, NULL, 0) == 0);
  sppc_timespec start, end;
  CHECK(sppc_clock_gettime(SPPC_CLOCK_MONOTONIC, &start) == 0);
  CHECK(sppc_pthread_mutex_lock(&cv_mutex) == 0);
  int rc = 0;
  while (!cv_go && rc == 0) { rc = sppc_pthread_cond_clockwait(&cv, &cv_mutex, SPPC_CLOCK_MONOTONIC, &long_wait); }
  CHECK(rc == 0 && cv_go);
  CHECK(sppc_pthread_mutex_unlock(&cv_mutex) == 0);
  CHECK(sppc_clock_gettime(SPPC_CLOCK_MONOTONIC, &end) == 0 && end.sec - start.sec < 4);
  CHECK(sppc_pthread_join(&t[0]) == 0);

  CHECK(sppc_pthread_cond_destroy(&cv) == 0 && sppc_pthread_mutex_destroy(&cv_mutex) == 0);
}

// ==================== RWLOCKS, SPIN LOCKS, BARRIERS ====================

static uint64_t rw, spin, holder_ready, holder_release;
static int other_rc = -1;

static void hold_write(void *e) {
  (void)e;
  sppc_pthread_rwlock_wrlock(&rw);
  sppc_pthread_barrier_wait(&holder_ready);
  sppc_pthread_barrier_wait(&holder_release);
  sppc_pthread_rwlock_unlock(&rw);
}

static void try_read(void *e) {
  (void)e;
  other_rc = sppc_pthread_rwlock_tryrdlock(&rw);
  if (other_rc == 0) { sppc_pthread_rwlock_unlock(&rw); }
}

static void try_write(void *e) {
  (void)e;
  other_rc = sppc_pthread_rwlock_trywrlock(&rw);
  if (other_rc == 0) { sppc_pthread_rwlock_unlock(&rw); }
}

static void try_spin(void *e) {
  (void)e;
  other_rc = sppc_pthread_spin_trylock(&spin);
  if (other_rc == 0) { sppc_pthread_spin_unlock(&spin); }
}

static void run_and_join(void (*fn)(void*)) {
  uint64_t t;
  other_rc = -1;
  CHECK(spawn(&t, fn, NULL, 0) == 0 && sppc_pthread_join(&t) == 0);
}

static void check_rwlock(void) {
  CHECK(sppc_pthread_rwlock_init(&rw) == 0);
  CHECK(sppc_pthread_barrier_init(&holder_ready, 2) == 0 && sppc_pthread_barrier_init(&holder_release, 2) == 0);

  // Readers share; a writer is kept out while one reads.
  CHECK(sppc_pthread_rwlock_rdlock(&rw) == 0);
  run_and_join(try_read);
  CHECK(other_rc == 0);
  run_and_join(try_write);
  CHECK(other_rc == SPPC_EBUSY);
  CHECK(sppc_pthread_rwlock_unlock(&rw) == 0);
  run_and_join(try_write);
  CHECK(other_rc == 0);

  // A writer elsewhere keeps readers out; a timed read gives up.
  uint64_t t;
  CHECK(spawn(&t, hold_write, NULL, 0) == 0);
  sppc_pthread_barrier_wait(&holder_ready);
  CHECK(sppc_pthread_rwlock_tryrdlock(&rw) == SPPC_EBUSY);
  CHECK(sppc_pthread_rwlock_trywrlock(&rw) == SPPC_EBUSY);
  const sppc_timespec d = {0, 20000000};
  CHECK(sppc_pthread_rwlock_clockrdlock(&rw, SPPC_CLOCK_MONOTONIC, &d) == SPPC_ETIMEDOUT);
  CHECK(sppc_pthread_rwlock_clockwrlock(&rw, SPPC_CLOCK_MONOTONIC, &d) == SPPC_ETIMEDOUT);
  sppc_pthread_barrier_wait(&holder_release);
  CHECK(sppc_pthread_join(&t) == 0);
  CHECK(sppc_pthread_rwlock_clockrdlock(&rw, SPPC_CLOCK_MONOTONIC, &d) == 0);
  CHECK(sppc_pthread_rwlock_unlock(&rw) == 0);

  CHECK(sppc_pthread_barrier_destroy(&holder_ready) == 0 && sppc_pthread_barrier_destroy(&holder_release) == 0);
  CHECK(sppc_pthread_rwlock_destroy(&rw) == 0);
}

static void check_spin(void) {
  CHECK(sppc_pthread_spin_init(&spin) == 0);
  CHECK(sppc_pthread_spin_lock(&spin) == 0);
  CHECK(sppc_pthread_spin_trylock(&spin) == SPPC_EBUSY);
  run_and_join(try_spin);
  CHECK(other_rc == SPPC_EBUSY);
  CHECK(sppc_pthread_spin_unlock(&spin) == 0);
  run_and_join(try_spin);
  CHECK(other_rc == 0);
  CHECK(sppc_pthread_spin_destroy(&spin) == 0);
}

static uint64_t barrier;
static _Atomic int serial = 0, passed = 0;

static void meet(void *e) {
  (void)e;
  const int rc = sppc_pthread_barrier_wait(&barrier);
  if (rc == SPPC_BARRIER_SERIAL_THREAD) { ++serial; }
  else if (rc == 0) { ++passed; }
}

static void check_barrier(void) {
  // Exactly one of those released is told it is the serial thread.
  CHECK(sppc_pthread_barrier_init(&barrier, 4) == 0);
  uint64_t t[4];
  for (int i = 0; i < 4; ++i) { CHECK(spawn(&t[i], meet, NULL, 0) == 0); }
  for (int i = 0; i < 4; ++i) { CHECK(sppc_pthread_join(&t[i]) == 0); }
  CHECK(serial == 1 && passed == 3);
  CHECK(sppc_pthread_barrier_destroy(&barrier) == 0);
  CHECK(sppc_pthread_barrier_init(&barrier, 0) == SPPC_EINVAL);
  sppc_sched_yield();
}

// ==================== LOCKS FROM A TASK ====================

// A task of the async runtime wants a lock another os thread holds. With no
// other task to run, it has to wait for the thread, not spin forever or fail.

static uint64_t shared_mutex;
static int task_got_it = 0;

static void hold_shared(void *e) {
  (void)e;
  sppc_pthread_mutex_lock(&shared_mutex);
  sppc_pthread_barrier_wait(&holder_ready);
  sleep_ms(30);
  sppc_pthread_mutex_unlock(&shared_mutex);
}

static void task_takes_it(void *e) {
  (void)e;
  if (sppc_pthread_mutex_lock(&shared_mutex) == 0) {
    task_got_it = 1;
    sppc_pthread_mutex_unlock(&shared_mutex);
  }
}

static void check_task_waits_for_thread(void) {
  CHECK(sppc_pthread_mutex_init(&shared_mutex) == 0);
  CHECK(sppc_pthread_barrier_init(&holder_ready, 2) == 0);
  uint64_t t;
  CHECK(spawn(&t, hold_shared, NULL, 0) == 0);
  sppc_pthread_barrier_wait(&holder_ready);
  int h = -1;
  CHECK(sppc_async(&h, (sppc_closure){task_takes_it, NULL}) == 0);
  CHECK(sppc_await(h) == 0);
  CHECK(task_got_it == 1);
  CHECK(sppc_pthread_join(&t) == 0);
  CHECK(sppc_pthread_barrier_destroy(&holder_ready) == 0);
  CHECK(sppc_pthread_mutex_destroy(&shared_mutex) == 0);
}

int main(void) {
  if (sppc_init() != 0) { return 1; }
  check_create_join();
  check_detach();
  check_once();
  check_mutexes();
  check_conditions();
  check_rwlock();
  check_spin();
  check_barrier();
  check_task_waits_for_thread();
  sppc_cleanup();
  return check_done();
}
