#include <sppc/sppc.h>
#include <assert.h>

// Two tasks on one thread contending for a thread lock. A plain blocking lock
// would stop the only thread that can ever release it, so the waiter has to
// let the holder run instead, and the release has to run the waiter next:
// each holder takes its lock straight back after releasing it, and without
// that hand-off it would win the lock again ahead of the task waiting on it.

static char log_[64];
static int log_n = 0;
static void note(char c) { log_[log_n++] = c; }

static uint64_t mtx, rw, spin;

static void mutex_holder(void *e) {
  (void)e;
  assert(sppc_pthread_mutex_lock(&mtx) == 0);
  note('A');
  sppc_async_yield();
  sppc_async_yield();
  note('a');
  assert(sppc_pthread_mutex_unlock(&mtx) == 0);
  assert(sppc_pthread_mutex_lock(&mtx) == 0);
  note('C');
  assert(sppc_pthread_mutex_unlock(&mtx) == 0);
}

static void mutex_waiter(void *e) {
  (void)e;
  assert(sppc_pthread_mutex_lock(&mtx) == 0);
  note('B');
  assert(sppc_pthread_mutex_unlock(&mtx) == 0);
}

static void rwlock_writer(void *e) {
  (void)e;
  assert(sppc_pthread_rwlock_wrlock(&rw) == 0);
  note('W');
  sppc_async_yield();
  note('w');
  assert(sppc_pthread_rwlock_unlock(&rw) == 0);
  assert(sppc_pthread_rwlock_wrlock(&rw) == 0);
  note('X');
  assert(sppc_pthread_rwlock_unlock(&rw) == 0);
}

static void rwlock_reader(void *e) {
  (void)e;
  assert(sppc_pthread_rwlock_rdlock(&rw) == 0);
  note('R');
  assert(sppc_pthread_rwlock_unlock(&rw) == 0);
}

static void spin_holder(void *e) {
  (void)e;
  assert(sppc_pthread_spin_lock(&spin) == 0);
  note('S');
  sppc_async_yield();
  note('s');
  assert(sppc_pthread_spin_unlock(&spin) == 0);
  assert(sppc_pthread_spin_lock(&spin) == 0);
  note('U');
  assert(sppc_pthread_spin_unlock(&spin) == 0);
}

static void spin_waiter(void *e) {
  (void)e;
  assert(sppc_pthread_spin_lock(&spin) == 0);
  note('T');
  assert(sppc_pthread_spin_unlock(&spin) == 0);
}

// The timed waits: the holder keeps the lock well past the waiter's 20ms
// deadline, yielding as it goes so the waiter gets to see the time run out.
static int timed_mutex_rc = -1;
static int timed_rwlock_rc = -1;

static void hold_mutex_long(void *e) {
  (void)e;
  assert(sppc_pthread_mutex_lock(&mtx) == 0);
  for (int i = 0; i < 50; ++i) {
    const struct timespec ts = {0, 2000000};
    nanosleep(&ts, NULL);
    sppc_async_yield();
  }
  assert(sppc_pthread_mutex_unlock(&mtx) == 0);
}

static void timed_mutex(void *e) {
  (void)e;
  const sppc_timespec d = {0, 20000000};
  timed_mutex_rc = sppc_pthread_mutex_clocklock(&mtx, SPPC_CLOCK_MONOTONIC, &d);
}

static void hold_rwlock_long(void *e) {
  (void)e;
  assert(sppc_pthread_rwlock_wrlock(&rw) == 0);
  for (int i = 0; i < 50; ++i) {
    const struct timespec ts = {0, 2000000};
    nanosleep(&ts, NULL);
    sppc_async_yield();
  }
  assert(sppc_pthread_rwlock_unlock(&rw) == 0);
}

static void timed_rwlock(void *e) {
  (void)e;
  const sppc_timespec d = {0, 20000000};
  timed_rwlock_rc = sppc_pthread_rwlock_clockwrlock(&rw, SPPC_CLOCK_MONOTONIC, &d);
}

static void run_pair(void (*first)(void*), void (*second)(void*)) {
  int a, b;
  assert(sppc_async(&a, (sppc_closure){first, NULL}) == 0);
  assert(sppc_async(&b, (sppc_closure){second, NULL}) == 0);
  assert(sppc_await(a) == 0);
  assert(sppc_await(b) == 0);
}

int main(void) {
  assert(sppc_init() == 0);
  assert(sppc_pthread_mutex_init(&mtx) == 0);
  assert(sppc_pthread_rwlock_init(&rw) == 0);
  assert(sppc_pthread_spin_init(&spin) == 0);

  // The holder takes the lock and yields; the waiter finds it held, runs the
  // holder rather than blocking, and gets the lock as soon as it is released -
  // before the holder's own attempt to take it back.
  run_pair(mutex_holder, mutex_waiter);
  run_pair(rwlock_writer, rwlock_reader);
  run_pair(spin_holder, spin_waiter);
  log_[log_n] = '\0';
  printf("order=%s (want AaBCWwRXSsTU)\n", log_);
  assert(strcmp(log_, "AaBCWwRXSsTU") == 0);

  run_pair(hold_mutex_long, timed_mutex);
  printf("timed mutex=%d (want SPPC_ETIMEDOUT=%d)\n", timed_mutex_rc, SPPC_ETIMEDOUT);
  assert(timed_mutex_rc == SPPC_ETIMEDOUT);

  run_pair(hold_rwlock_long, timed_rwlock);
  printf("timed rwlock=%d (want SPPC_ETIMEDOUT=%d)\n", timed_rwlock_rc, SPPC_ETIMEDOUT);
  assert(timed_rwlock_rc == SPPC_ETIMEDOUT);

  assert(sppc_pthread_spin_destroy(&spin) == 0);
  assert(sppc_pthread_rwlock_destroy(&rw) == 0);
  assert(sppc_pthread_mutex_destroy(&mtx) == 0);
  assert(sppc_cleanup() == 0);
  printf("ok\n");
  return 0;
}
