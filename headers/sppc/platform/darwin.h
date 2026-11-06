#pragma once

/// The parts of the public api that Darwin implements with
/// calls of its own, rather than the POSIX ones in
/// platform/posix.h: barriers, the calls
/// that are Linux syscalls there ("pipe2", "accept4",
/// "sendfile", "copy_file_range", "getrandom",
/// "clock_nanosleep"), built from what Darwin has, and
/// process hardening, which it has no switch for. Linux
/// defines the same functions in platform/linux.h. Ahead
/// of platform/posix.h come what that needs of Darwin's
/// own: "madvise"'s DONTNEED, and the pthread calls the
/// async runtime takes its locks through.

#include <sppc/config.h>
#include <sppc/macros.h>
#include <sppc/types.h>
#include <sppc/errors.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>

// ==================== MEMORY ====================

// Ahead of platform/posix.h, whose "sppc_madvise" calls it.
//
// Linux's MADV_DONTNEED drops a private mapping's pages, so anonymous memory
// reads back as zero and what was in it is gone. Darwin's is a hint that
// leaves it all in place. So private anonymous memory has fresh pages mapped
// over it, with its protection, maximum protection, inheritance and tag, and
// the old pages go back to the kernel. Shared memory keeps its contents on
// Linux too, so it gets the hint alone, as does file-backed memory - which,
// unlike Linux, keeps any private writes to it rather than reading the file
// again. As on Linux, locked pages refuse it. So does executable anonymous
// memory, which only "MAP_JIT" could map again, and that is not known here.
// Either refusal comes before anything is replaced.

#include <mach/mach.h>
#include <mach/vm_statistics.h>
#include <stdbool.h>
#include <sys/mman.h>

/// One region of the address space, and whether DONTNEED replaces it.
typedef struct {
  vm_address_t start, end;
  vm_region_basic_info_data_64_t basic;
  vm_region_extended_info_data_t extended;
  bool replace;
} _sppc_dontneed_region;

/// The region "at" is in, into "out". ENOMEM for an address nothing maps,
/// as Linux's madvise says for a hole.
_gnu_inline _gnu_nonnull(2)
int _sppc_dontneed_region_at(const vm_address_t at, _sppc_dontneed_region *_sppc_restrict out) {
  vm_address_t start = at, again;
  vm_size_t size = 0;
  mach_port_t object = MACH_PORT_NULL;
  mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
  if (vm_region_64(mach_task_self(), &start, &size, VM_REGION_BASIC_INFO_64, (vm_region_info_t)&out->basic, &count,
    &object) != KERN_SUCCESS || start > at) { return SPPC_ENOMEM; }
  again = start;
  count = VM_REGION_EXTENDED_INFO_COUNT;
  if (vm_region_64(mach_task_self(), &again, &size, VM_REGION_EXTENDED_INFO, (vm_region_info_t)&out->extended, &count,
    &object) != KERN_SUCCESS || again != start) { return SPPC_ENOMEM; }

  // Private: not shared on fork (MAP_SHARED is), and not shared now with
  // anything outside this map. Anonymous: no file anywhere behind it, its
  // copy-on-write chain included.
  const unsigned char mode = out->extended.share_mode;
  out->start = start;
  out->end = start + size;
  out->replace = out->basic.inheritance != VM_INHERIT_SHARE && !out->extended.external_pager
    && (mode == SM_PRIVATE || mode == SM_COW || mode == SM_PRIVATE_ALIASED || mode == SM_EMPTY);
  return 0;
}

_gnu_inline_va _gnu_nonnull(1)
int _sppc_madvise_dontneed(void *addr, const size_t length) {
  const vm_address_t page = (vm_address_t)vm_page_size - 1;
  const vm_address_t start = (vm_address_t)addr;
  if ((start & page) != 0) { return SPPC_EINVAL; }
  if (length == 0) { return 0; }
  const vm_address_t end = (start + length + page) & ~page;
  if (end <= start) { return SPPC_EINVAL; }
  if (madvise(addr, end - start, MADV_DONTNEED) != 0) { return _sppc_errno(errno); }

  _sppc_dontneed_region r;
  for (vm_address_t at = start; at < end; at = r.end) {
    const int err = _sppc_dontneed_region_at(at, &r);
    if (err != 0) { return err; }
    if (r.basic.user_wired_count != 0) { return SPPC_EINVAL; }
    if (r.replace && (r.basic.protection & VM_PROT_EXECUTE) != 0) { return SPPC_EINVAL; }
  }

  for (vm_address_t at = start; at < end; at = r.end) {
    const int err = _sppc_dontneed_region_at(at, &r);
    if (err != 0) { return err; }
    if (!r.replace) { continue; }
    const vm_address_t to = r.end < end ? r.end : end;
    const vm_size_t len = to - at;

    // MAP_FIXED swaps the old pages out in one step, keeping them if the
    // new mapping fails. For an anonymous map Darwin reads the fd as its tag.
    const int tag = r.extended.user_tag != 0 ? VM_MAKE_TAG(r.extended.user_tag) : -1;
    if (mmap((void*)at, len, r.basic.protection, MAP_FIXED | MAP_PRIVATE | MAP_ANON, tag, 0) == MAP_FAILED) {
      return _sppc_errno(errno);
    }
    // A new map can be given anything: a lower ceiling must not be lifted.
    if (r.basic.max_protection != VM_PROT_ALL
      && vm_protect(mach_task_self(), at, len, TRUE, r.basic.max_protection) != KERN_SUCCESS) { return SPPC_EIO; }
    if (r.basic.inheritance != VM_INHERIT_DEFAULT
      && vm_inherit(mach_task_self(), at, len, r.basic.inheritance) != KERN_SUCCESS) { return SPPC_EIO; }
  }
  return 0;
}

// ==================== PTHREADS ====================

// Darwin's own pthread gaps, ahead of platform/posix.h and the async runtime
// it includes, which take their locks through them: no timed lock of any
// kind, no spin lock, a once too big for its handle (built with FreeBSD's, in
// platform/posix.h), and a rwlock that refuses its own thread with EDEADLK.

#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <time.h>

// The pause between two tries of a lock or once built from tries: from
// 50 us, doubling up to 1 ms, and never past a deadline.
#define _SPPC_BACKOFF_MIN_NS 50000L
#define _SPPC_BACKOFF_MAX_NS 1000000L


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

// ==================== POSIX ====================

#include <sppc/platform/posix.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>

// ==================== THREADING ====================

/// Darwin has no barriers, so one is built from a mutex and a
/// condition. "cycle" tells a waiter woken by the release of
/// its own round from one woken spuriously, or one already
/// into the next round.
typedef struct {
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  unsigned count;
  unsigned arrived;
  unsigned cycle;
} _sppc_barrier;

_gnu_inline _gnu_restrict_access(write_only, 1)
_sppc_api int sppc_pthread_barrier_init(uint64_t *_sppc_restrict barrier, const uint64_t count) {
  if (count == 0 || count > UINT_MAX) { return SPPC_EINVAL; }
  _pthread_handle_alloc(_sppc_barrier)
  obj->count = (unsigned)count;
  obj->arrived = 0;
  obj->cycle = 0;
  int err = pthread_mutex_init(&obj->mutex, NULL);
  if (err == 0) {
    err = pthread_cond_init(&obj->cond, NULL);
    if (err != 0) { pthread_mutex_destroy(&obj->mutex); }
  }
  _pthread_handle_publish(barrier)
}

/// As glibc's: the thread's own wait, not the runtime's, so a
/// task waiting here holds up its whole thread.
_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_barrier_wait(uint64_t const *_sppc_restrict barrier) {
  _sppc_barrier *const b = _pthread_handle(_sppc_barrier, barrier);
  _return_if_pthread_err(pthread_mutex_lock(&b->mutex))
  const unsigned cycle = b->cycle;
  if (++b->arrived == b->count) {
    b->arrived = 0;
    ++b->cycle;
    pthread_cond_broadcast(&b->cond);
    pthread_mutex_unlock(&b->mutex);
    return SPPC_BARRIER_SERIAL_THREAD;
  }
  int err = 0;
  while (err == 0 && cycle == b->cycle) { err = pthread_cond_wait(&b->cond, &b->mutex); }
  pthread_mutex_unlock(&b->mutex);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_barrier_destroy(uint64_t const *_sppc_restrict barrier) {
  _sppc_barrier *const obj = _pthread_handle(_sppc_barrier, barrier);
  pthread_cond_destroy(&obj->cond);
  _extract_err(int) pthread_mutex_destroy(&obj->mutex);
  free(obj);
  _return_normalized_pthread_err
}

// ==================== SYSCALLS ====================

/// A pipe whose ends close on exec, as "pipe2(O_CLOEXEC)".
_gnu_inline _gnu_nonnull(1)
int _sppc_pipe_cloexec(int fds[2]) {
  if (pipe(fds) != 0) { return -1; }
  if (_sppc_fd_set_flags(fds[0], true, false) != 0 || _sppc_fd_set_flags(fds[1], true, false) != 0) {
    const int err = errno;
    close(fds[0]);
    close(fds[1]);
    errno = err;
    return -1;
  }
  return 0;
}

_posix_syscall(22)
_gnu_inline _gnu_restrict_access(write_only, 1) _gnu_restrict_access(write_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_pipe(int *_sppc_restrict out_read_fd, int *_sppc_restrict out_write_fd) {
  int fds[2];
  _extract_err(int) _sppc_pipe_cloexec(fds);
  if (err == 0) {
    *out_read_fd = fds[0];
    *out_write_fd = fds[1];
  }
  _return_normalized_err
}

/// Linux's "sendfile" from Darwin's, which only sends to a
/// socket, takes the offset by value, never moves the file
/// position, and reads 0 bytes as "to the end of the file".
/// It reports what it sent through "len" even when it fails
/// part way, which counts as sent. Any target other than a
/// socket is a read and a write.
_gnu_inline
ssize_t _sppc_sendfile(const int to_fd, const int from_fd, int64_t *offset, const size_t count) {
  if (count == 0) { return 0; }
  const off_t start = offset != NULL ? (off_t)*offset : lseek(from_fd, 0, SEEK_CUR);
  if (start < 0) { return -1; }

  ssize_t sent;
  off_t len = count > (size_t)INT64_MAX ? (off_t)INT64_MAX : (off_t)count;
  if (sendfile(from_fd, to_fd, start, &len, NULL, 0) == 0 || len > 0) { sent = (ssize_t)len; }
  else if (errno == ENOTSOCK) {
    sent = _sppc_send_by_copy(to_fd, from_fd, start, count);
    if (sent <= 0) { return sent; }
  }
  else { return -1; }

  if (offset != NULL) { *offset = (int64_t)(start + sent); }
  else if (lseek(from_fd, start + sent, SEEK_SET) < 0) { return -1; }
  return sent;
}

_posix_syscall(40)
_gnu_inline _gnu_fd_arg_read(1) _gnu_fd_arg_write(2) _gnu_restrict_access(write_only, 5) _gnu_nonnull(5)
_sppc_api int sppc_sendfile(const int from_fd, const int to_fd, int64_t *offset, const size_t count,
  ssize_t *_sppc_restrict out_n) {
  _extract_err(ssize_t) _sppc_sendfile(to_fd, from_fd, offset, count);
  _sret_normalised_store(out_n)
  _return_normalized_err
}

/// "accept4(O_CLOEXEC)": the flag set straight after.
_gnu_inline
int _sppc_accept_cloexec(const int fd, struct sockaddr *addr, socklen_t *len) {
  const int out = accept(fd, addr, len);
  if (out < 0) { return -1; }
  if (_sppc_fd_set_flags(out, true, false) != 0) {
    const int err = errno;
    close(out);
    errno = err;
    return -1;
  }
  return out;
}

_posix_syscall(43)
_gnu_inline _gnu_fd_arg(1) _gnu_restrict_access(write_only, 2) _gnu_restrict_access(write_only, 3) _gnu_nonnull(2, 3)
_sppc_api int sppc_accept(const int fd, sppc_sockaddr_storage *_sppc_restrict out_storage, int *_sppc_restrict out_fd) {
  struct sockaddr *const addr = (struct sockaddr*)_sppc_os_sockaddr(out_storage);
  socklen_t len = (socklen_t)sizeof(struct sockaddr_storage);
  _extract_err_async(int, _gt_try_accept(fd, addr, &len, &_async_res), _sppc_accept_cloexec(fd, addr, &len))
  _sret_normalised_store(out_fd)
  _return_normalized_err
}

/// Darwin declares no "fdatasync"; "fsync" also writes the
/// metadata, which is more than asked and still correct.
_posix_syscall(75)
_gnu_inline _gnu_fd_arg(1)
_sppc_api int sppc_fdatasync(const int fd) {
  _extract_err(int) fsync(fd);
  _return_normalized_err
}

/// Darwin has no "clock_nanosleep": "nanosleep" for the time
/// left, in the shape of the calls routed through
/// "_extract_err_async" (-1 and errno). An absolute deadline
/// is re-read after each sleep, so a real-time clock set back
/// meanwhile still sleeps until it. A process's or thread's
/// cpu clock does not move while it sleeps, so cannot be
/// slept on.
_gnu_inline _gnu_nonnull(3)
int _sppc_clock_nanosleep_errno(const clockid_t clock, const int flags, struct timespec const *duration) {
  if (clock == CLOCK_PROCESS_CPUTIME_ID || clock == CLOCK_THREAD_CPUTIME_ID) {
    errno = EINVAL;
    return -1;
  }
  if ((flags & TIMER_ABSTIME) == 0) { return nanosleep(duration, NULL); }
  for (;;) {
    struct timespec left;
    const int err = _sppc_time_left(clock, duration, &left);
    if (err == ETIMEDOUT) { return 0; }
    if (err != 0) {
      errno = err;
      return -1;
    }
    if (nanosleep(&left, NULL) != 0) { return -1; }
  }
}

_posix_syscall(230)
_gnu_inline _gnu_restrict_access(read_only, 3) _gnu_nonnull(3)
_sppc_api int sppc_clock_nanosleep(const int clock, const int flags, sppc_timespec const *_sppc_restrict duration) {
  clockid_t os_clock;
  int os_flags;
  struct timespec ts;
  if (_sppc_to_os_clock(clock, &os_clock) != 0 || _sppc_to_os_timer_flags(flags, &os_flags) != 0 ||
    _sppc_to_os_timespec(duration, &ts) != 0) {
    return SPPC_EINVAL;
  }
  _extract_err_async(int, _gt_try_sleep(os_clock, os_flags, &ts, &_async_res),
    _sppc_clock_nanosleep_errno(os_clock, os_flags, &ts))
  _return_normalized_err
}

/// "arc4random_buf" draws from the kernel's generator, never
/// fails and never blocks, so there is no error to report.
_posix_syscall(318)
_gnu_inline _gnu_restrict_access(write_only, 2) _gnu_nonnull(2)
_sppc_api int sppc_getrandom(const size_t size, char *_sppc_restrict out) {
  arc4random_buf(out, size);
  return 0;
}

// ==================== STDLIB ====================

_gnu_inline _gnu_fd_arg_read(1) _gnu_fd_arg_write(2) _gnu_restrict_access(write_only, 5) _gnu_nonnull(5)
_sppc_api int sppc_copyfile(const int fd_in, const int fd_out, const size_t len, const int flags,
  ssize_t *_sppc_restrict out_n) {
  // No flags are defined yet; the argument keeps room for them.
  if (flags != 0) {
    *out_n = -1;
    return SPPC_EINVAL;
  }
  // A partial copy is normal, so the count has to reach the caller.
  _extract_err(ssize_t) _sppc_copy_range(fd_in, fd_out, len);
  _sret_normalised_store(out_n)
  _return_normalized_err
}

// ==================== PROCESS HARDENING ====================

// Neither has a switch a process can turn on for itself on
// Darwin. Each fails the start-up, as on a Linux too old for
// it, naming the key that turns it off, rather than reporting
// a protection the process does not have.

_gnu_inline _gnu_cold
_sppc_api int sppc_deny_wx(void) {
  fprintf(stderr,
    "[sppc] could not deny writable-and-executable memory: macOS has no switch a process can set for it\n"
    "  (the hardened runtime, 'codesign --options runtime', enforces it when the program is signed).\n"
    "  To run without it, set 'deny_wx = false' under [memory.map] in spp.toml and rebuild.\n");
  return SPPC_ENOSYS;
}

_gnu_inline _gnu_cold
_sppc_api int sppc_seal_segments(void) {
  fprintf(stderr,
    "[sppc] could not seal the program's memory mappings: macOS has no mseal\n"
    "  To run without it, set 'seal = false' under [memory.map] in spp.toml and rebuild.\n");
  return SPPC_ENOSYS;
}
