#pragma once

/// The pthread calls sppc needs beyond POSIX: waits against a chosen clock,
/// spin locks, a once that fits in a handle, and try-locks that say EBUSY
/// for a lock this thread already holds. glibc has them all; Darwin has
/// none, so they are built here from what it does have. Each takes the glibc call's arguments and
/// returns as it does (0 or an error number), under an "_sppc_" name, so
/// nothing claims a pthread name a later SDK might declare itself.

#include <sppc/config.h>
#include <sppc/macros.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <time.h>

#if SPPC_KERNEL_LINUX

typedef pthread_spinlock_t _sppc_spinlock_t;
typedef pthread_once_t _sppc_once_t;
#define _SPPC_ONCE_INIT PTHREAD_ONCE_INIT
#define _sppc_pthread_once pthread_once
#define _sppc_pthread_rwlock_tryrdlock pthread_rwlock_tryrdlock
#define _sppc_pthread_rwlock_trywrlock pthread_rwlock_trywrlock
#define _sppc_pthread_mutex_clocklock pthread_mutex_clocklock
#define _sppc_pthread_rwlock_clockrdlock pthread_rwlock_clockrdlock
#define _sppc_pthread_rwlock_clockwrlock pthread_rwlock_clockwrlock
#define _sppc_pthread_cond_clockwait pthread_cond_clockwait
#define _sppc_pthread_spin_init pthread_spin_init
#define _sppc_pthread_spin_lock pthread_spin_lock
#define _sppc_pthread_spin_trylock pthread_spin_trylock
#define _sppc_pthread_spin_unlock pthread_spin_unlock
#define _sppc_pthread_spin_destroy pthread_spin_destroy

#elif SPPC_KERNEL_DARWIN

// ---------- deadlines ----------

/// How long is left until the absolute @p deadline on @p clock, into @p out.
/// ETIMEDOUT once it has passed, and EINVAL for a clock that cannot be read.
_gnu_inline _gnu_nonnull(2, 3)
int _sppc_time_left(const clockid_t clock, struct timespec const *deadline, struct timespec *out) {
  struct timespec now;
  if (clock_gettime(clock, &now) != 0) { return EINVAL; }
  out->tv_sec = deadline->tv_sec - now.tv_sec;
  out->tv_nsec = deadline->tv_nsec - now.tv_nsec;
  if (out->tv_nsec < 0) {
    out->tv_nsec += 1000000000L;
    out->tv_sec -= 1;
  }
  return out->tv_sec < 0 || (out->tv_sec == 0 && out->tv_nsec == 0) ? ETIMEDOUT : 0;
}

// The pause between two tries of a timed lock: from 50 us, doubling up to
// 1 ms, and never past the deadline.
#define _SPPC_BACKOFF_MIN_NS 50000L
#define _SPPC_BACKOFF_MAX_NS 1000000L

/// Wait out one pause of a timed lock, or report its deadline as passed.
_gnu_inline_va _gnu_nonnull(2, 3)
int _sppc_backoff(const clockid_t clock, struct timespec const *deadline, long *pause_ns) {
  struct timespec left;
  const int err = _sppc_time_left(clock, deadline, &left);
  if (err != 0) { return err; }
  struct timespec nap = {0, *pause_ns};
  if (left.tv_sec == 0 && left.tv_nsec < nap.tv_nsec) { nap = left; }
  nanosleep(&nap, NULL);
  if (*pause_ns < _SPPC_BACKOFF_MAX_NS) { *pause_ns *= 2; }
  return 0;
}

// ---------- try-locks ----------

// Darwin's rwlock refuses a thread that already holds it for writing with
// EDEADLK, even to a try. Green tasks share their thread, so one task can
// meet a lock another task on the same thread holds; as on glibc, that is
// simply a lock that is taken, and the task waits for it like any other.

_gnu_inline _gnu_nonnull(1)
int _sppc_pthread_rwlock_tryrdlock(pthread_rwlock_t *rwlock) {
  const int err = pthread_rwlock_tryrdlock(rwlock);
  return err == EDEADLK ? EBUSY : err;
}

_gnu_inline _gnu_nonnull(1)
int _sppc_pthread_rwlock_trywrlock(pthread_rwlock_t *rwlock) {
  const int err = pthread_rwlock_trywrlock(rwlock);
  return err == EDEADLK ? EBUSY : err;
}

// ---------- timed locks ----------

// Darwin has no timed lock of any kind, clock-based or not, so each one
// tries, then pauses, until it gets the lock or the deadline passes. A
// waiter is therefore up to a pause late to a released lock, and is not
// queued: a stream of other lockers can keep taking it first. A timed
// lock is the rare case; the untimed ones block properly.

#define _SPPC_TIMED_LOCK(try_call) do {                                  \
  long pause_ = _SPPC_BACKOFF_MIN_NS;                                    \
  for (;;) {                                                             \
    const int err_ = (try_call);                                         \
    if (err_ != EBUSY) { return err_; }                                  \
    const int wait_ = _sppc_backoff(clock, deadline, &pause_);           \
    if (wait_ != 0) { return wait_; }                                    \
  }                                                                      \
} while (0)

_gnu_inline_va _gnu_nonnull(1, 3)
int _sppc_pthread_mutex_clocklock(pthread_mutex_t *mutex, const clockid_t clock, struct timespec const *deadline) {
  _SPPC_TIMED_LOCK(pthread_mutex_trylock(mutex));
}

_gnu_inline_va _gnu_nonnull(1, 3)
int _sppc_pthread_rwlock_clockrdlock(pthread_rwlock_t *rwlock, const clockid_t clock, struct timespec const *deadline) {
  _SPPC_TIMED_LOCK(_sppc_pthread_rwlock_tryrdlock(rwlock));
}

_gnu_inline_va _gnu_nonnull(1, 3)
int _sppc_pthread_rwlock_clockwrlock(pthread_rwlock_t *rwlock, const clockid_t clock, struct timespec const *deadline) {
  _SPPC_TIMED_LOCK(_sppc_pthread_rwlock_trywrlock(rwlock));
}

#undef _SPPC_TIMED_LOCK

/// A real-time deadline is what "pthread_cond_timedwait" takes. Any other
/// clock's becomes the time left on it, which Darwin's relative wait takes.
_gnu_inline _gnu_nonnull(1, 2, 4)
int _sppc_pthread_cond_clockwait(pthread_cond_t *cond, pthread_mutex_t *mutex, const clockid_t clock,
  struct timespec const *deadline) {
  if (clock == CLOCK_REALTIME) { return pthread_cond_timedwait(cond, mutex, deadline); }
  struct timespec left;
  const int err = _sppc_time_left(clock, deadline, &left);
  if (err != 0) { return err; }
  return pthread_cond_timedwait_relative_np(cond, mutex, &left);
}

// ---------- spin locks ----------

/// An int, as glibc's is, so it too fits in the handle rather than behind it.
typedef int _sppc_spinlock_t;

_gnu_inline _gnu_nonnull(1)
int _sppc_pthread_spin_init(_sppc_spinlock_t *spinlock, const int pshared) {
  (void)pshared;
  __atomic_store_n(spinlock, 0, __ATOMIC_RELAXED);
  return 0;
}

_gnu_inline _gnu_nonnull(1)
int _sppc_pthread_spin_trylock(_sppc_spinlock_t *spinlock) {
  return __atomic_exchange_n(spinlock, 1, __ATOMIC_ACQUIRE) == 0 ? 0 : EBUSY;
}

/// Spins on a plain load, so the waiting core only reads its own cached copy
/// of the line until the holder's release, and gives the cpu up now and then
/// in case the holder was descheduled.
_gnu_inline _gnu_nonnull(1)
int _sppc_pthread_spin_lock(_sppc_spinlock_t *spinlock) {
  while (__atomic_exchange_n(spinlock, 1, __ATOMIC_ACQUIRE) != 0) {
    for (unsigned spins = 1; __atomic_load_n(spinlock, __ATOMIC_RELAXED) != 0; ++spins) {
#if SPPC_ARCH_X86_64
      __builtin_ia32_pause();
#elif SPPC_ARCH_ARM64
      __asm__ volatile("yield");
#endif
      if (spins % 1024 == 0) { sched_yield(); }
    }
  }
  return 0;
}

_gnu_inline _gnu_nonnull(1)
int _sppc_pthread_spin_unlock(_sppc_spinlock_t *spinlock) {
  __atomic_store_n(spinlock, 0, __ATOMIC_RELEASE);
  return 0;
}

_gnu_inline _gnu_nonnull(1)
int _sppc_pthread_spin_destroy(_sppc_spinlock_t *spinlock) {
  (void)spinlock;
  return 0;
}

// ---------- once ----------

/// Darwin's "pthread_once_t" is 16 bytes, too big for the handle it has to
/// live in, so once is built on an int: not run, running, or done. The
/// thread that moves it from not run to running calls the function; any
/// other waits it out with the timed locks' pauses, as a once is run once
/// and seldom raced for.
typedef int _sppc_once_t;
#define _SPPC_ONCE_INIT 0
#define _SPPC_ONCE_RUNNING 1
#define _SPPC_ONCE_DONE 2

_gnu_inline_va _gnu_nonnull(1, 2)
int _sppc_pthread_once(_sppc_once_t *once, void (*fn)(void)) {
  int state = _SPPC_ONCE_INIT;
  if (__atomic_compare_exchange_n(once, &state, _SPPC_ONCE_RUNNING, false, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE)) {
    fn();
    __atomic_store_n(once, _SPPC_ONCE_DONE, __ATOMIC_RELEASE);
    return 0;
  }
  for (long pause = _SPPC_BACKOFF_MIN_NS; __atomic_load_n(once, __ATOMIC_ACQUIRE) != _SPPC_ONCE_DONE;) {
    const struct timespec nap = {0, pause};
    nanosleep(&nap, NULL);
    if (pause < _SPPC_BACKOFF_MAX_NS) { pause *= 2; }
  }
  return 0;
}

#endif
