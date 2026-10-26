#pragma once

/// The parts of the public api that Linux and FreeBSD both implement with
/// calls beyond POSIX, which both have: barriers, "pipe2", "accept4",
/// "fdatasync", "clock_nanosleep", "getrandom" and "copy_file_range".
/// Included by platform/linux.h and platform/freebsd.h, after
/// platform/posix.h; Darwin builds the same functions in platform/darwin.h.

#include <sppc/platform/posix.h>
#include <sys/random.h>

// ==================== THREADING ====================

_gnu_inline _gnu_restrict_access(write_only, 1)
_sppc_api int sppc_pthread_barrier_init(uint64_t *_sppc_restrict barrier, const uint64_t count) {
  _pthread_handle_alloc(pthread_barrier_t)
  _extract_err(int) pthread_barrier_init(obj, NULL, count);
  _pthread_handle_publish(barrier)
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_barrier_wait(uint64_t const *_sppc_restrict barrier) {
  _extract_err(int) pthread_barrier_wait(_pthread_handle(pthread_barrier_t, barrier));
  if (err == PTHREAD_BARRIER_SERIAL_THREAD) { return SPPC_BARRIER_SERIAL_THREAD; }
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_barrier_destroy(uint64_t const *_sppc_restrict barrier) {
  pthread_barrier_t *const obj = _pthread_handle(pthread_barrier_t, barrier);
  _extract_err(int) pthread_barrier_destroy(obj);
  free(obj);
  _return_normalized_pthread_err
}

// ==================== SYSCALLS ====================

_posix_syscall(22)
_gnu_inline _gnu_restrict_access(write_only, 1) _gnu_restrict_access(write_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_pipe(int *_sppc_restrict out_read_fd, int *_sppc_restrict out_write_fd) {
  int fds[2];
  _extract_err(int) pipe2(fds, O_CLOEXEC);
  if (err == 0) {
    *out_read_fd = fds[0];
    *out_write_fd = fds[1];
  }
  _return_normalized_err
}

_posix_syscall(43)
_gnu_inline _gnu_fd_arg(1) _gnu_restrict_access(write_only, 2) _gnu_restrict_access(write_only, 3) _gnu_nonnull(2, 3)
_sppc_api int sppc_accept(const int fd, sppc_sockaddr_storage *_sppc_restrict out_storage, int *_sppc_restrict out_fd) {
  struct sockaddr *const addr = (struct sockaddr*)_sppc_os_sockaddr(out_storage);
  socklen_t len = (socklen_t)sizeof(struct sockaddr_storage);
  _extract_err_async(int, _gt_try_accept(fd, addr, &len, &_async_res), accept4(fd, addr, &len, SOCK_CLOEXEC))
  _sret_normalised_store(out_fd)
  _return_normalized_err
}

_posix_syscall(75)
_gnu_inline _gnu_fd_arg(1)
_sppc_api int sppc_fdatasync(const int fd) {
  _extract_err(int) fdatasync(fd);
  _return_normalized_err
}

// "clock_nanosleep" returns its error rather than setting errno, unlike every
// other call routed through "_extract_err_async"; this gives it their shape.
_gnu_inline _gnu_nonnull(3)
int _sppc_clock_nanosleep_errno(const clockid_t clock, const int flags, struct timespec const *duration) {
  const int e = clock_nanosleep(clock, flags, duration, NULL);
  if (e != 0) {
    errno = e;
    return -1;
  }
  return 0;
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

_posix_syscall(318)
_gnu_inline _gnu_restrict_access(write_only, 2) _gnu_nonnull(2)
_sppc_api int sppc_getrandom(const size_t size, char *_sppc_restrict out) {
  size_t filled = 0;
  while (filled < size) {
    const ssize_t n = getrandom(out + filled, size - filled, 0);
    if (n < 0) {
      if (errno == EINTR) { continue; }
      return _sppc_errno(errno);
    }
    filled += (size_t)n;
  }
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
  _extract_err(ssize_t) copy_file_range(fd_in, NULL, fd_out, NULL, len, 0);
  _sret_normalised_store(out_n)
  _return_normalized_err
}
