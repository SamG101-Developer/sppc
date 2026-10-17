#pragma once

/// The parts of the public api that Linux implements with
/// calls of its own, rather than the POSIX ones in
/// platform/posix.h: glibc's clock-based pthread waits,
/// barriers and spin locks, and the Linux syscalls ("pipe2",
/// "accept4", "sendfile", "copy_file_range", "getrandom",
/// ...). Darwin defines the same functions in platform/darwin.h.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <sppc/platform/posix.h>
#include <sys/prctl.h>
#include <sys/random.h>
#include <sys/sendfile.h>

_Static_assert(
  sizeof(off_t) == sizeof(int64_t), "sppc: off_t is not 64-bit (build with _FILE_OFFSET_BITS=64)");

// ==================== ERRORS ====================

// The sppc error numbers are Linux's, which is what lets
// "_sppc_errno" return the platform's unchanged here. Every one
// of them is checked; a mismatch is a compile error, not a
// wrong error at run time.
#define _SPPC_ERRNO_DIFFERS "sppc: errno differs"
_Static_assert(SPPC_EPERM == EPERM, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ENOENT == ENOENT, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ESRCH == ESRCH, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EINTR == EINTR, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EIO == EIO, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ENXIO == ENXIO, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_E2BIG == E2BIG, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ENOEXEC == ENOEXEC, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EBADF == EBADF, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ECHILD == ECHILD, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EAGAIN == EAGAIN, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ENOMEM == ENOMEM, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EACCES == EACCES, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EFAULT == EFAULT, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ENOTBLK == ENOTBLK, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EBUSY == EBUSY, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EEXIST == EEXIST, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EXDEV == EXDEV, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ENODEV == ENODEV, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ENOTDIR == ENOTDIR, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EISDIR == EISDIR, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EINVAL == EINVAL, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ENFILE == ENFILE, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EMFILE == EMFILE, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ENOTTY == ENOTTY, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ETXTBSY == ETXTBSY, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EFBIG == EFBIG, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ENOSPC == ENOSPC, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ESPIPE == ESPIPE, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EROFS == EROFS, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EMLINK == EMLINK, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EPIPE == EPIPE, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EDOM == EDOM, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ERANGE == ERANGE, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EDEADLK == EDEADLK, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ENAMETOOLONG == ENAMETOOLONG, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ENOLCK == ENOLCK, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ENOSYS == ENOSYS, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ENOTEMPTY == ENOTEMPTY, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ELOOP == ELOOP, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EWOULDBLOCK == EWOULDBLOCK, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ENOMSG == ENOMSG, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EIDRM == EIDRM, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ECHRNG == ECHRNG, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EL2NSYNC == EL2NSYNC, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EL3HLT == EL3HLT, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EL3RST == EL3RST, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ELNRNG == ELNRNG, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EUNATCH == EUNATCH, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ENOCSI == ENOCSI, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EL2HLT == EL2HLT, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EBADE == EBADE, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EBADR == EBADR, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EXFULL == EXFULL, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ENOANO == ENOANO, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EBADRQC == EBADRQC, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EBADSLT == EBADSLT, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EDEADLOCK == EDEADLOCK, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EBFONT == EBFONT, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ENOSTR == ENOSTR, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ENODATA == ENODATA, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ETIME == ETIME, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ENOSR == ENOSR, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ENONET == ENONET, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ENOPKG == ENOPKG, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EREMOTE == EREMOTE, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ENOLINK == ENOLINK, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EADV == EADV, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ESRMNT == ESRMNT, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ECOMM == ECOMM, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EPROTO == EPROTO, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EMULTIHOP == EMULTIHOP, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EDOTDOT == EDOTDOT, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EBADMSG == EBADMSG, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EOVERFLOW == EOVERFLOW, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ENOTUNIQ == ENOTUNIQ, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EBADFD == EBADFD, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EREMCHG == EREMCHG, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ELIBACC == ELIBACC, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ELIBBAD == ELIBBAD, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ELIBSCN == ELIBSCN, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ELIBMAX == ELIBMAX, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ELIBEXEC == ELIBEXEC, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EILSEQ == EILSEQ, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ERESTART == ERESTART, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ESTRPIPE == ESTRPIPE, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EUSERS == EUSERS, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ENOTSOCK == ENOTSOCK, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EDESTADDRREQ == EDESTADDRREQ, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EMSGSIZE == EMSGSIZE, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EPROTOTYPE == EPROTOTYPE, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ENOPROTOOPT == ENOPROTOOPT, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EPROTONOSUPPORT == EPROTONOSUPPORT, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ESOCKTNOSUPPORT == ESOCKTNOSUPPORT, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EOPNOTSUPP == EOPNOTSUPP, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EPFNOSUPPORT == EPFNOSUPPORT, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EAFNOSUPPORT == EAFNOSUPPORT, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EADDRINUSE == EADDRINUSE, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EADDRNOTAVAIL == EADDRNOTAVAIL, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ENETDOWN == ENETDOWN, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ENETUNREACH == ENETUNREACH, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ENETRESET == ENETRESET, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ECONNABORTED == ECONNABORTED, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ECONNRESET == ECONNRESET, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ENOBUFS == ENOBUFS, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EISCONN == EISCONN, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ENOTCONN == ENOTCONN, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ESHUTDOWN == ESHUTDOWN, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ETOOMANYREFS == ETOOMANYREFS, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ETIMEDOUT == ETIMEDOUT, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ECONNREFUSED == ECONNREFUSED, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EHOSTDOWN == EHOSTDOWN, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EHOSTUNREACH == EHOSTUNREACH, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EALREADY == EALREADY, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EINPROGRESS == EINPROGRESS, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ESTALE == ESTALE, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EUCLEAN == EUCLEAN, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ENOTNAM == ENOTNAM, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ENAVAIL == ENAVAIL, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EISNAM == EISNAM, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EREMOTEIO == EREMOTEIO, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EDQUOT == EDQUOT, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ENOMEDIUM == ENOMEDIUM, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EMEDIUMTYPE == EMEDIUMTYPE, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ECANCELED == ECANCELED, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ENOKEY == ENOKEY, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EKEYEXPIRED == EKEYEXPIRED, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EKEYREVOKED == EKEYREVOKED, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EKEYREJECTED == EKEYREJECTED, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EOWNERDEAD == EOWNERDEAD, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ENOTRECOVERABLE == ENOTRECOVERABLE, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_ERFKILL == ERFKILL, _SPPC_ERRNO_DIFFERS);
_Static_assert(SPPC_EHWPOISON == EHWPOISON, _SPPC_ERRNO_DIFFERS);
#undef _SPPC_ERRNO_DIFFERS

// ==================== THREADING ====================

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(read_only, 3)
_sppc_api int sppc_pthread_mutex_clocklock(uint64_t const *_sppc_restrict mutex, const int clock,
  sppc_timespec const *_sppc_restrict duration) {
  clockid_t os_clock;
  struct timespec os_duration, deadline;
  if (_sppc_to_os_clock(clock, &os_clock) != 0 || _sppc_to_os_timespec(duration, &os_duration) != 0) { return SPPC_EINVAL; }
  _gt_deadline(os_clock, &os_duration, &deadline);
  _extract_err(int) _gt_mutex_lock_until(_pthread_handle(pthread_mutex_t, mutex), os_clock, &deadline);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(read_only, 2) _gnu_restrict_access(read_only, 4)
_sppc_api int sppc_pthread_cond_clockwait(uint64_t const *_sppc_restrict cond, uint64_t const *_sppc_restrict mutex,
  const int clock, sppc_timespec const *_sppc_restrict duration) {
  clockid_t os_clock;
  struct timespec os_duration, deadline;
  if (_sppc_to_os_clock(clock, &os_clock) != 0 || _sppc_to_os_timespec(duration, &os_duration) != 0) { return SPPC_EINVAL; }
  _gt_deadline(os_clock, &os_duration, &deadline);
  _extract_err(int) _gt_cond_wait_until(
    _pthread_handle(pthread_cond_t, cond),
    _pthread_handle(pthread_mutex_t, mutex),
    os_clock, &deadline);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(read_only, 3)
_sppc_api int sppc_pthread_rwlock_clockrdlock(uint64_t const *_sppc_restrict rwlock, const int clock,
  sppc_timespec const *_sppc_restrict duration) {
  clockid_t os_clock;
  struct timespec os_duration, deadline;
  if (_sppc_to_os_clock(clock, &os_clock) != 0 || _sppc_to_os_timespec(duration, &os_duration) != 0) { return SPPC_EINVAL; }
  _gt_deadline(os_clock, &os_duration, &deadline);
  _extract_err(int) _gt_rwlock_rdlock_until(_pthread_handle(pthread_rwlock_t, rwlock), os_clock, &deadline);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(read_only, 3)
_sppc_api int sppc_pthread_rwlock_clockwrlock(uint64_t const *_sppc_restrict rwlock, const int clock,
  sppc_timespec const *_sppc_restrict duration) {
  clockid_t os_clock;
  struct timespec os_duration, deadline;
  if (_sppc_to_os_clock(clock, &os_clock) != 0 || _sppc_to_os_timespec(duration, &os_duration) != 0) { return SPPC_EINVAL; }
  _gt_deadline(os_clock, &os_duration, &deadline);
  _extract_err(int) _gt_rwlock_wrlock_until(_pthread_handle(pthread_rwlock_t, rwlock), os_clock, &deadline);
  _return_normalized_pthread_err
}

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

_gnu_inline _gnu_restrict_access(write_only, 1)
// As for "once": a "pthread_spinlock_t" is an int, so it lives in the handle rather than behind it.
_sppc_api int sppc_pthread_spin_init(uint64_t *_sppc_restrict spinlock) {
  _extract_err(int) pthread_spin_init((pthread_spinlock_t*)spinlock, 0);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_spin_lock(uint64_t const *_sppc_restrict spinlock) {
  _extract_err(int) _gt_spin_lock((pthread_spinlock_t*)spinlock);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_spin_trylock(uint64_t const *_sppc_restrict spinlock) {
  _extract_err(int) pthread_spin_trylock((pthread_spinlock_t*)spinlock);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_spin_unlock(uint64_t const *_sppc_restrict spinlock) {
  _extract_err(int) _gt_spin_unlock((pthread_spinlock_t*)spinlock);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_spin_destroy(uint64_t const *_sppc_restrict spinlock) {
  _extract_err(int) pthread_spin_destroy((pthread_spinlock_t*)spinlock);
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

_posix_syscall(40)
_gnu_inline _gnu_fd_arg_read(1) _gnu_fd_arg_write(2) _gnu_restrict_access(write_only, 5) _gnu_nonnull(5)
_sppc_api int sppc_sendfile(const int from_fd, const int to_fd, int64_t *offset, const size_t count,
  ssize_t *_sppc_restrict out_n) {
  _extract_err(ssize_t) sendfile(to_fd, from_fd, (off_t*)offset, count);
  _sret_normalised_store(out_n)
  _return_normalized_err
}

_posix_syscall(43)
_gnu_inline _gnu_fd_arg(1) _gnu_restrict_access(write_only, 2) _gnu_restrict_access(write_only, 3) _gnu_nonnull(2, 3)
_sppc_api int sppc_accept(const int fd, sppc_sockaddr_storage *_sppc_restrict out_storage, int *_sppc_restrict out_fd) {
  struct sockaddr *const addr = (struct sockaddr*)_sppc_os_sockaddr(out_storage);
  socklen_t len = (socklen_t)sizeof(struct sockaddr_storage);
  _extract_err_async(int, _gt_try_accept(fd, addr, &len, &_async_res), accept4(fd, addr, &len, O_CLOEXEC))
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

// ==================== PROCESS HARDENING ====================

// Older headers predate MDWE; the numbers are the kernel's ABI.
#ifndef PR_SET_MDWE
#define PR_SET_MDWE 65
#endif
#ifndef PR_MDWE_REFUSE_EXEC_GAIN
#define PR_MDWE_REFUSE_EXEC_GAIN (1UL << 0)
#endif
#ifndef PR_MDWE_NO_INHERIT
#define PR_MDWE_NO_INHERIT (1UL << 1)
#endif

_gnu_inline _gnu_cold
_sppc_api int sppc_deny_wx(void) {
  if (prctl(PR_SET_MDWE, PR_MDWE_REFUSE_EXEC_GAIN | PR_MDWE_NO_INHERIT, 0L, 0L, 0L) == 0) { return 0; }
  const int err = errno;
  fprintf(stderr,
    "[sppc] could not deny writable-and-executable memory (PR_SET_MDWE needs Linux 6.6+): %s\n"
    "  To run without it, set 'deny_wx = false' under [memory.map] in spp.toml and rebuild.\n",
    strerror(err));
  return err;
}
