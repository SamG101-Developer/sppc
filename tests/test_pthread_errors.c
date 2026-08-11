// Regression test: pthread wrappers must return the pthread error code.
//
// pthread functions return their error number directly and never touch errno,
// so the wrappers must propagate the return value. errno is deliberately
// poisoned before every call: if the old `return errno` behaviour comes back,
// these assertions report 12345.

#include <sppc/sppc.h>
#include "harness.h"

#define POISON 12345

int main(void) {
  TEST_BEGIN("pthread error propagation");
  sppc_init();

  uint64_t m[8] = {0};
  errno = POISON; CHECK_EQ("mutex_init", sppc_pthread_mutex_init(m), 0);
  errno = POISON; CHECK_EQ("mutex_lock(fresh)", sppc_pthread_mutex_lock(m), 0);
  errno = POISON; CHECK_EQ("mutex_trylock(held) -> special 1", sppc_pthread_mutex_trylock(m), 1);
  errno = POISON; CHECK_EQ("mutex_destroy(held) -> EBUSY", sppc_pthread_mutex_destroy(m), EBUSY);
  errno = POISON; CHECK_EQ("mutex_unlock(held)", sppc_pthread_mutex_unlock(m), 0);
  errno = POISON; CHECK_EQ("mutex_destroy(free)", sppc_pthread_mutex_destroy(m), 0);

  // A recursive mutex gives a defined EPERM when unlocked by a non-owner.
  errno = POISON; CHECK_EQ("mutex_init_recursive", sppc_pthread_mutex_init_recursive(m), 0);
  errno = POISON; CHECK_EQ("mutex_unlock(not owned) -> EPERM", sppc_pthread_mutex_unlock(m), EPERM);
  sppc_pthread_mutex_destroy(m);

  uint64_t b[8] = {0};
  errno = POISON; CHECK_EQ("barrier_init(count=0) -> EINVAL", sppc_pthread_barrier_init(b, 0), EINVAL);
  errno = POISON; CHECK_EQ("barrier_init(count=1)", sppc_pthread_barrier_init(b, 1), 0);
  errno = POISON; CHECK_EQ("barrier_destroy", sppc_pthread_barrier_destroy(b), 0);

  uint64_t rw[8] = {0};
  errno = POISON; CHECK_EQ("rwlock_init", sppc_pthread_rwlock_init(rw), 0);
  sppc_pthread_rwlock_wrlock(rw);
  errno = POISON; CHECK_EQ("rwlock_trywrlock(held) -> special 1", sppc_pthread_rwlock_trywrlock(rw), 1);
  errno = POISON; CHECK_EQ("rwlock_unlock", sppc_pthread_rwlock_unlock(rw), 0);
  errno = POISON; CHECK_EQ("rwlock_destroy", sppc_pthread_rwlock_destroy(rw), 0);

  uint64_t sp[8] = {0};
  errno = POISON; CHECK_EQ("spin_init", sppc_pthread_spin_init(sp), 0);
  errno = POISON; CHECK_EQ("spin_lock", sppc_pthread_spin_lock(sp), 0);
  errno = POISON; CHECK_EQ("spin_trylock(held) -> special 1", sppc_pthread_spin_trylock(sp), 1);
  errno = POISON; CHECK_EQ("spin_unlock", sppc_pthread_spin_unlock(sp), 0);
  errno = POISON; CHECK_EQ("spin_destroy", sppc_pthread_spin_destroy(sp), 0);

  errno = POISON; CHECK_EQ("sppc_cleanup()", sppc_cleanup(), 0);

  return TEST_SUMMARY();
}