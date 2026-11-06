#pragma once

/// The POSIX implementation of the public api, shared by Linux, Darwin and
/// FreeBSD, in the order its parts need each other: the system headers and
/// the pthread calls sppc needs beyond POSIX, then the async runtime and the
/// crash reporter that stand on them, then the api, and last the parts every
/// platform shares (platform/common.h). A function that differs between the
/// platforms by a line or two has an "#if" here; one that differs throughout
/// is defined whole in that platform's own file instead (platform/linux.h,
/// platform/darwin.h, platform/freebsd.h), and this one skips it with an
/// "#if", so every platform exports the same set of functions. Each
/// platform's file includes this one.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <sppc/config.h>
#include <sppc/macros.h>
#include <sppc/closure.h>
#include <sppc/types.h>
#include <sppc/errors.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#if SPPC_KERNEL_FREEBSD
  #include <pthread_np.h>
#endif
#include <sched.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/uio.h>
#if defined(__GLIBC__)
#include <malloc.h>
#endif

// ==================== PTHREADS ====================

// The pthread calls sppc needs beyond POSIX: waits against a chosen clock,
// spin locks, a once that fits in a handle, and try-locks that say EBUSY for
// a lock this thread already holds. glibc has them all; musl lacks the
// clock-based waits, FreeBSD them and a once that fits, and Darwin has none,
// so they are built from what each does have: here where two platforms share
// the way, in platform/darwin.h (ahead of this file) for Darwin's own. Each
// takes the glibc call's arguments and returns as it does (0 or an error
// number), under an "_sppc_" name, so nothing claims a pthread name a later
// SDK might declare itself. The async runtime, included after this, takes
// its locks through them.

#ifndef _SPPC_BACKOFF_MIN_NS // platform/darwin.h's, if it came first
// The pause between two tries of a lock or once built from tries: from
// 50 us, doubling up to 1 ms, and never past a deadline.
#define _SPPC_BACKOFF_MIN_NS 50000L
#define _SPPC_BACKOFF_MAX_NS 1000000L
#endif

#if SPPC_KERNEL_LINUX || SPPC_KERNEL_FREEBSD

typedef pthread_spinlock_t _sppc_spinlock_t;
#define _sppc_pthread_rwlock_tryrdlock pthread_rwlock_tryrdlock
#define _sppc_pthread_rwlock_trywrlock pthread_rwlock_trywrlock
#define _sppc_pthread_spin_init pthread_spin_init
#define _sppc_pthread_spin_lock pthread_spin_lock
#define _sppc_pthread_spin_trylock pthread_spin_trylock
#define _sppc_pthread_spin_unlock pthread_spin_unlock
#define _sppc_pthread_spin_destroy pthread_spin_destroy

#endif

#if SPPC_KERNEL_LINUX

typedef pthread_once_t _sppc_once_t;
#define _SPPC_ONCE_INIT PTHREAD_ONCE_INIT
#define _sppc_pthread_once pthread_once

#endif

#if defined(__GLIBC__)

#define _sppc_pthread_mutex_clocklock pthread_mutex_clocklock
#define _sppc_pthread_rwlock_clockrdlock pthread_rwlock_clockrdlock
#define _sppc_pthread_rwlock_clockwrlock pthread_rwlock_clockwrlock
#define _sppc_pthread_cond_clockwait pthread_cond_clockwait

#elif SPPC_KERNEL_LINUX || SPPC_KERNEL_FREEBSD

// musl and FreeBSD have only the POSIX timed waits, against CLOCK_REALTIME
// (the clock a condition variable made with no attributes waits on). A
// deadline on any other clock becomes the real-time instant as far off as it
// is: a step of the real-time clock during the wait moves it, as glibc's
// calls would not.

/// @p deadline on @p clock as a real-time deadline, into @p out. EINVAL for
/// a clock that cannot be read.
_gnu_inline _gnu_nonnull(2, 3)
int _sppc_realtime_deadline(
  const clockid_t clock, struct timespec const *deadline,
  struct timespec *out) {
  if (clock == CLOCK_REALTIME) {
    *out = *deadline;
    return 0;
  }
  struct timespec now, real;
  if (clock_gettime(clock, &now) != 0 || clock_gettime(CLOCK_REALTIME, &real) != 0) { return EINVAL; }
  out->tv_sec = real.tv_sec + (deadline->tv_sec - now.tv_sec);
  out->tv_nsec = real.tv_nsec + (deadline->tv_nsec - now.tv_nsec);
  while (out->tv_nsec < 0) {
    out->tv_nsec += 1000000000L;
    out->tv_sec -= 1;
  }
  while (out->tv_nsec >= 1000000000L) {
    out->tv_nsec -= 1000000000L;
    out->tv_sec += 1;
  }
  return 0;
}

_gnu_inline _gnu_nonnull(1, 3)
int _sppc_pthread_mutex_clocklock(
  pthread_mutex_t *mutex, const clockid_t clock,
  struct timespec const *deadline) {
  struct timespec real;
  const int err = _sppc_realtime_deadline(clock, deadline, &real);
  return err != 0 ? err : pthread_mutex_timedlock(mutex, &real);
}

_gnu_inline _gnu_nonnull(1, 3)
int _sppc_pthread_rwlock_clockrdlock(
  pthread_rwlock_t *rwlock, const clockid_t clock,
  struct timespec const *deadline) {
  struct timespec real;
  const int err = _sppc_realtime_deadline(clock, deadline, &real);
  return err != 0 ? err : pthread_rwlock_timedrdlock(rwlock, &real);
}

_gnu_inline _gnu_nonnull(1, 3)
int _sppc_pthread_rwlock_clockwrlock(
  pthread_rwlock_t *rwlock, const clockid_t clock,
  struct timespec const *deadline) {
  struct timespec real;
  const int err = _sppc_realtime_deadline(clock, deadline, &real);
  return err != 0 ? err : pthread_rwlock_timedwrlock(rwlock, &real);
}

_gnu_inline _gnu_nonnull(1, 2, 4)
int _sppc_pthread_cond_clockwait(
  pthread_cond_t *cond, pthread_mutex_t *mutex,
  const clockid_t clock, struct timespec const *deadline) {
  struct timespec real;
  const int err = _sppc_realtime_deadline(clock, deadline, &real);
  return err != 0 ? err : pthread_cond_timedwait(cond, mutex, &real);
}

#endif

#if SPPC_KERNEL_DARWIN || SPPC_KERNEL_FREEBSD

/// Darwin's and FreeBSD's "pthread_once_t" is 16 bytes, too big for the
/// handle it has to live in, so once is built on an int: not run, running,
/// or done. The thread that moves it from not run to running calls the
/// function; any other waits it out with the backoff's pauses, as a once is
/// run once and seldom raced for.
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

// ==================== RUNTIME ====================

// The async runtime and the crash reporter, over the pthreads above; the api
// below takes its blocking calls and locks through them.
#include <sppc/async.h>
#include <sppc/backtrace.h>

extern pthread_mutex_t _stdin_mutex;
extern pthread_mutex_t _stdout_mutex;
extern pthread_mutex_t _stderr_mutex;

// "MAP_STACK" is a hint only Linux and the BSDs take.
#if defined(MAP_STACK)
  #define _SPPC_MAP_STACK MAP_STACK
#else
  #define _SPPC_MAP_STACK 0
#endif

/// "getenv", except in a setuid/setgid process, which trusts nothing in its
/// environment and sees none of it. glibc has this as "secure_getenv".
_gnu_inline _gnu_nonnull(1)
char* _sppc_secure_getenv(char const *name) {
#if defined(__GLIBC__)
  return secure_getenv(name);
#else
  return issetugid() ? NULL : getenv(name);
#endif
}

/// Make @p fd close on exec, and non-blocking if @p nonblock,
/// for the calls that cannot be asked to do it as they make
/// the descriptor (Darwin's "socket", "accept", "pipe").
/// Another thread forking in between still inherits it; there
/// is no closing that gap without the flags.
_gnu_inline_va
int _sppc_fd_set_flags(const int fd, const bool cloexec, const bool nonblock) {
  if (cloexec && fcntl(fd, F_SETFD, FD_CLOEXEC) != 0) { return -1; }
  if (nonblock) {
    const int fl = fcntl(fd, F_GETFL);
    if (fl < 0 || fcntl(fd, F_SETFL, fl | O_NONBLOCK) != 0) { return -1; }
  }
  return 0;
}

// What the copying fallbacks for "sendfile" and "copy_file_range" move per
// call: small, as the call may be on a task's stack, and a short count is
// allowed of both calls they stand in for.
#define _SPPC_COPY_CHUNK 4096

/// Up to a chunk of @p count bytes of @p from_fd at @p start, written to
/// @p to_fd: "sendfile" to a descriptor the BSDs' own cannot send to, as it
/// only sends to a socket. Neither file position moves.
_gnu_inline
ssize_t _sppc_send_by_copy(const int to_fd, const int from_fd, const off_t start, const size_t count) {
  char buf[_SPPC_COPY_CHUNK];
  const ssize_t got = pread(from_fd, buf, count < sizeof buf ? count : sizeof buf, start);
  if (got <= 0) { return got; }
  return write(to_fd, buf, (size_t)got);
}

/// "copy_file_range" from each file's position: a read and a
/// write, each moving its position as the real call does. Darwin
/// has no "copy_file_range"; Linux and FreeBSD fall back to this
/// where theirs refuses the two files. A
/// write that falls short puts the read position back by what
/// was not written, so nothing read is lost.
_gnu_inline
ssize_t _sppc_copy_range(const int fd_in, const int fd_out, const size_t len) {
  char buf[_SPPC_COPY_CHUNK];
  if (len == 0) { return 0; }
  const ssize_t got = read(fd_in, buf, len < sizeof buf ? len : sizeof buf);
  if (got <= 0) { return got; }
  ssize_t done = 0;
  while (done < got) {
    const ssize_t w = write(fd_out, buf + done, (size_t)(got - done));
    if (w < 0) {
      if (errno == EINTR) { continue; }
      break;
    }
    done += w;
  }
  if (done < got) {
    const int err = errno;
    lseek(fd_in, (off_t)(done - got), SEEK_CUR);
    if (done == 0) {
      errno = err;
      return -1;
    }
  }
  return done;
}

// ==================== TRANSLATION ====================

// The machinery, and the tables every platform shares (sockets, poll), are
// platform/shared.h's; what follows is the POSIX platforms' own.
#include <sppc/platform/shared.h>

// ---------- values every POSIX system shares ----------

_Static_assert(SPPC_S_IFMT == S_IFMT && SPPC_S_IFSOCK == S_IFSOCK && SPPC_S_IFLNK == S_IFLNK &&
  SPPC_S_IFREG == S_IFREG && SPPC_S_IFBLK == S_IFBLK && SPPC_S_IFDIR == S_IFDIR && SPPC_S_IFCHR == S_IFCHR &&
  SPPC_S_IFIFO == S_IFIFO && SPPC_S_ISUID == S_ISUID && SPPC_S_ISGID == S_ISGID && SPPC_S_ISVTX == S_ISVTX &&
  SPPC_S_IRWXU == S_IRWXU && SPPC_S_IRWXG == S_IRWXG && SPPC_S_IRWXO == S_IRWXO,
  "sppc: file mode bits differ on this platform");
_Static_assert(SPPC_F_OK == F_OK && SPPC_R_OK == R_OK && SPPC_W_OK == W_OK && SPPC_X_OK == X_OK,
  "sppc: access modes differ on this platform");
_Static_assert(SPPC_SEEK_SET == SEEK_SET && SPPC_SEEK_CUR == SEEK_CUR && SPPC_SEEK_END == SEEK_END,
  "sppc: seek origins differ on this platform");
_Static_assert(SPPC_PROT_NONE == PROT_NONE && SPPC_PROT_READ == PROT_READ && SPPC_PROT_WRITE == PROT_WRITE &&
  SPPC_PROT_EXEC == PROT_EXEC, "sppc: memory protections differ on this platform");
_Static_assert(SPPC_IPPROTO_IP == IPPROTO_IP && SPPC_IPPROTO_ICMP == IPPROTO_ICMP && SPPC_IPPROTO_TCP == IPPROTO_TCP &&
  SPPC_IPPROTO_UDP == IPPROTO_UDP && SPPC_IPPROTO_IPV6 == IPPROTO_IPV6 && SPPC_IPPROTO_ICMPV6 == IPPROTO_ICMPV6 &&
  SPPC_IPPROTO_RAW == IPPROTO_RAW, "sppc: protocol numbers differ on this platform");
_Static_assert(SPPC_SHUT_RD == SHUT_RD && SPPC_SHUT_WR == SHUT_WR && SPPC_SHUT_RDWR == SHUT_RDWR,
  "sppc: shutdown directions differ on this platform");
_Static_assert(SPPC_FD_CLOEXEC == FD_CLOEXEC, "sppc: FD_CLOEXEC differs on this platform");

#define _SPPC_MODE_PERMISSIONS 07777
#define _SPPC_ACCESS_MODES (SPPC_R_OK | SPPC_W_OK | SPPC_X_OK)

// ---------- files ----------

/// "sppc_open" flags, which F_GETFL / F_SETFL share.
_gnu_inline
_sppc_pair const* _sppc_open_flags_table(size_t *_sppc_restrict n) {
  static const _sppc_pair table[] = {
#ifdef O_TMPFILE
    _SPPC_PAIR(O_TMPFILE),
#endif
    _SPPC_PAIR(O_SYNC), _SPPC_PAIR(O_CREAT), _SPPC_PAIR(O_EXCL), _SPPC_PAIR(O_NOCTTY), _SPPC_PAIR(O_TRUNC),
    _SPPC_PAIR(O_APPEND), _SPPC_PAIR(O_NONBLOCK), _SPPC_PAIR(O_DSYNC), _SPPC_PAIR(O_ASYNC),
#ifdef O_DIRECT
    _SPPC_PAIR(O_DIRECT),
#endif
    _SPPC_PAIR(O_DIRECTORY), _SPPC_PAIR(O_NOFOLLOW),
#ifdef O_NOATIME
    _SPPC_PAIR(O_NOATIME),
#endif
    _SPPC_PAIR(O_CLOEXEC),
#ifdef O_PATH
    _SPPC_PAIR(O_PATH),
#endif
  };
  *n = _SPPC_COUNT(table);
  return table;
}

_gnu_inline _gnu_nonnull(2)
int _sppc_to_os_open_flags(const int flags, int *_sppc_restrict out) {
  // The access mode is a value in the low bits, not a flag.
  static const _sppc_pair modes[] = {_SPPC_PAIR(O_RDONLY), _SPPC_PAIR(O_WRONLY), _SPPC_PAIR(O_RDWR)};
  int mode;
  if (_sppc_to_os_value(flags & SPPC_O_ACCMODE, modes, _SPPC_COUNT(modes), &mode) != 0) { return SPPC_EINVAL; }
  size_t n;
  _sppc_pair const *const table = _sppc_open_flags_table(&n);
  int rest;
  if (_sppc_to_os_flags(flags & ~SPPC_O_ACCMODE, table, n, &rest) != 0) { return SPPC_EINVAL; }
  *out = mode | rest;
  return 0;
}

_gnu_inline
int _sppc_from_os_open_flags(const int flags) {
  static const _sppc_pair modes[] = {_SPPC_PAIR(O_RDONLY), _SPPC_PAIR(O_WRONLY), _SPPC_PAIR(O_RDWR)};
  size_t n;
  _sppc_pair const *const table = _sppc_open_flags_table(&n);
  // The access mode's bits, not O_ACCMODE: musl's includes O_PATH (as its
  // O_SEARCH), which would be read as a mode and lost as a flag.
  const int access = O_RDONLY | O_WRONLY | O_RDWR;
  return _sppc_from_os_value(flags & access, modes, _SPPC_COUNT(modes), 0) |
    _sppc_from_os_flags(flags & ~access, table, n);
}

/// A mode passed in (to create or chmod): permission bits only.
_gnu_inline _gnu_nonnull(2)
int _sppc_to_os_mode(const uint32_t mode, mode_t *_sppc_restrict out) {
  if ((mode & ~(uint32_t)_SPPC_MODE_PERMISSIONS) != 0) { return SPPC_EINVAL; }
  *out = (mode_t)mode;
  return 0;
}

_gnu_inline _gnu_nonnull(1, 2)
void _sppc_from_os_stat(struct stat const *_sppc_restrict in, struct sppc_stat *_sppc_restrict out) {
  out->dev = (uint64_t)in->st_dev;
  out->ino = (uint64_t)in->st_ino;
  out->nlink = (uint64_t)in->st_nlink;
  out->rdev = (uint64_t)in->st_rdev;
  out->size = (int64_t)in->st_size;
  out->block_size = (int64_t)in->st_blksize;
  out->blocks = (int64_t)in->st_blocks;
#if SPPC_KERNEL_DARWIN
  out->atim = (sppc_timespec){(uint64_t)in->st_atimespec.tv_sec, (uint64_t)in->st_atimespec.tv_nsec};
  out->mtim = (sppc_timespec){(uint64_t)in->st_mtimespec.tv_sec, (uint64_t)in->st_mtimespec.tv_nsec};
  out->ctim = (sppc_timespec){(uint64_t)in->st_ctimespec.tv_sec, (uint64_t)in->st_ctimespec.tv_nsec};
#else
  out->atim = (sppc_timespec){(uint64_t)in->st_atim.tv_sec, (uint64_t)in->st_atim.tv_nsec};
  out->mtim = (sppc_timespec){(uint64_t)in->st_mtim.tv_sec, (uint64_t)in->st_mtim.tv_nsec};
  out->ctim = (sppc_timespec){(uint64_t)in->st_ctim.tv_sec, (uint64_t)in->st_ctim.tv_nsec};
#endif
  out->mode = (uint32_t)in->st_mode;
  out->uid = (uint32_t)in->st_uid;
  out->gid = (uint32_t)in->st_gid;
  out->_reserved = 0;
}

_gnu_inline _gnu_nonnull(1, 2)
void _sppc_from_os_statvfs(struct statvfs const *_sppc_restrict in, struct sppc_statvfs *_sppc_restrict out) {
  static const _sppc_pair flags[] = {
    _SPPC_PAIR(ST_RDONLY), _SPPC_PAIR(ST_NOSUID),
#ifdef ST_NODEV
    _SPPC_PAIR(ST_NODEV),
#endif
#ifdef ST_NOEXEC
    _SPPC_PAIR(ST_NOEXEC),
#endif
#ifdef ST_SYNCHRONOUS
    _SPPC_PAIR(ST_SYNCHRONOUS),
#endif
#ifdef ST_MANDLOCK
    _SPPC_PAIR(ST_MANDLOCK),
#endif
#ifdef ST_NOATIME
    _SPPC_PAIR(ST_NOATIME),
#endif
#ifdef ST_NODIRATIME
    _SPPC_PAIR(ST_NODIRATIME),
#endif
#ifdef ST_RELATIME
    _SPPC_PAIR(ST_RELATIME),
#endif
  };
  out->block_size = (uint64_t)in->f_bsize;
  out->frag_size = (uint64_t)in->f_frsize;
  out->blocks = (uint64_t)in->f_blocks;
  out->blocks_free = (uint64_t)in->f_bfree;
  out->blocks_available = (uint64_t)in->f_bavail;
  out->files = (uint64_t)in->f_files;
  out->files_free = (uint64_t)in->f_ffree;
  out->files_available = (uint64_t)in->f_favail;
  out->fs_id = (uint64_t)in->f_fsid;
  out->flag = (uint64_t)_sppc_from_os_flags((int)in->f_flag, flags, _SPPC_COUNT(flags));
  out->namemax = (uint64_t)in->f_namemax;
}

/// "sppc_utimensat" flags.
_gnu_inline _gnu_nonnull(2)
int _sppc_to_os_at_flags(const int flags, int *_sppc_restrict out) {
  static const _sppc_pair table[] = {_SPPC_PAIR(AT_SYMLINK_NOFOLLOW)};
  return _sppc_to_os_flags(flags, table, _SPPC_COUNT(table), out);
}

// ---------- descriptors ----------

/// A record lock's type.
_gnu_inline
_sppc_pair const* _sppc_lock_types_table(size_t *_sppc_restrict n) {
  static const _sppc_pair table[] = {_SPPC_PAIR(F_RDLCK), _SPPC_PAIR(F_WRLCK), _SPPC_PAIR(F_UNLCK)};
  *n = _SPPC_COUNT(table);
  return table;
}

// The layouts "sppc_poll", "sppc_readv"/"_writev" and "sppc_select" pass
// straight through: every POSIX system lays these out the same way, but for
// "sppc_select" on a 32-bit big-endian one (below).
_Static_assert(sizeof(sppc_pollfd) == sizeof(struct pollfd) && offsetof(struct pollfd, fd) == offsetof(sppc_pollfd, fd)
  && offsetof(struct pollfd, events) == offsetof(sppc_pollfd, events)
  && offsetof(struct pollfd, revents) == offsetof(sppc_pollfd, revents), "sppc: struct pollfd differs");
_Static_assert(sizeof(sppc_iovec) == sizeof(struct iovec) && offsetof(struct iovec, iov_base) == offsetof(sppc_iovec, base)
  && offsetof(struct iovec, iov_len) == offsetof(sppc_iovec, len), "sppc: struct iovec differs");
_Static_assert(sizeof(sppc_fd_set) == sizeof(fd_set) && FD_SETSIZE == 1024, "sppc: fd_set differs");

/// "sppc_poll" events, both ways.

// ---------- memory ----------

_gnu_inline _gnu_nonnull(2)
int _sppc_to_os_map_flags(const int flags, int *_sppc_restrict out) {
  static const _sppc_pair table[] = {
    _SPPC_PAIR(MAP_SHARED), _SPPC_PAIR(MAP_PRIVATE), _SPPC_PAIR(MAP_FIXED), _SPPC_PAIR(MAP_ANONYMOUS),
    {SPPC_MAP_NORESERVE, _SPPC_MAP_NORESERVE},
  };
  return _sppc_to_os_flags(flags, table, _SPPC_COUNT(table), out);
}

_gnu_inline _gnu_nonnull(2)
int _sppc_to_os_msync_flags(const int flags, int *_sppc_restrict out) {
  static const _sppc_pair table[] = {_SPPC_PAIR(MS_ASYNC), _SPPC_PAIR(MS_INVALIDATE), _SPPC_PAIR(MS_SYNC)};
  return _sppc_to_os_flags(flags, table, _SPPC_COUNT(table), out);
}

_gnu_inline _gnu_nonnull(2)
int _sppc_to_os_madvise(const int advice, int *_sppc_restrict out) {
  static const _sppc_pair table[] = {
    _SPPC_PAIR(MADV_NORMAL), _SPPC_PAIR(MADV_RANDOM), _SPPC_PAIR(MADV_SEQUENTIAL), _SPPC_PAIR(MADV_WILLNEED),
    _SPPC_PAIR(MADV_DONTNEED),
#ifdef MADV_FREE
    _SPPC_PAIR(MADV_FREE),
#endif
  };
  return _sppc_to_os_value(advice, table, _SPPC_COUNT(table), out);
}

// ---------- time ----------

_gnu_inline _gnu_nonnull(2)
int _sppc_to_os_clock(const int clock, clockid_t *_sppc_restrict out) {
  static const _sppc_pair table[] = {
    _SPPC_PAIR(CLOCK_REALTIME), _SPPC_PAIR(CLOCK_MONOTONIC), _SPPC_PAIR(CLOCK_PROCESS_CPUTIME_ID),
    _SPPC_PAIR(CLOCK_THREAD_CPUTIME_ID),
#ifdef CLOCK_MONOTONIC_RAW
    _SPPC_PAIR(CLOCK_MONOTONIC_RAW),
#endif
#ifdef CLOCK_REALTIME_COARSE
    _SPPC_PAIR(CLOCK_REALTIME_COARSE),
#endif
#ifdef CLOCK_MONOTONIC_COARSE
    _SPPC_PAIR(CLOCK_MONOTONIC_COARSE),
#endif
#ifdef CLOCK_BOOTTIME
    _SPPC_PAIR(CLOCK_BOOTTIME),
#endif
  };
  int os;
  if (_sppc_to_os_value(clock, table, _SPPC_COUNT(table), &os) != 0) { return SPPC_EINVAL; }
  *out = (clockid_t)os;
  return 0;
}

_gnu_inline _gnu_nonnull(1, 2)
int _sppc_to_os_timespec(sppc_timespec const *_sppc_restrict in, struct timespec *_sppc_restrict out) {
  if (in->nsec >= 1000000000u || in->sec > (uint64_t)INT64_MAX) { return SPPC_EINVAL; }
  out->tv_sec = (time_t)in->sec;
  out->tv_nsec = (long)in->nsec;
  return 0;
}

/// A time before 1970 has no sppc representation, so reads as 1970.
_gnu_inline _gnu_nonnull(1, 2)
void _sppc_from_os_timespec(struct timespec const *_sppc_restrict in, sppc_timespec *_sppc_restrict out) {
  out->sec = in->tv_sec < 0 ? 0 : (uint64_t)in->tv_sec;
  out->nsec = in->tv_sec < 0 ? 0 : (uint64_t)in->tv_nsec;
}

_gnu_inline _gnu_nonnull(1, 2)
int _sppc_to_os_timeval(sppc_timeval const *_sppc_restrict in, struct timeval *_sppc_restrict out) {
  if (in->usec >= 1000000u || in->sec > (uint64_t)INT64_MAX) { return SPPC_EINVAL; }
  out->tv_sec = (time_t)in->sec;
  out->tv_usec = (suseconds_t)in->usec;
  return 0;
}

_gnu_inline _gnu_nonnull(1, 2)
void _sppc_from_os_timeval(struct timeval const *_sppc_restrict in, sppc_timeval *_sppc_restrict out) {
  out->sec = in->tv_sec < 0 ? 0 : (uint64_t)in->tv_sec;
  out->usec = in->tv_sec < 0 ? 0 : (uint64_t)in->tv_usec;
}

// Darwin has no "clock_nanosleep", so no TIMER_ABSTIME either; the one
// platform/darwin.h builds reads this.
#if SPPC_KERNEL_DARWIN && !defined(TIMER_ABSTIME)
  #define TIMER_ABSTIME 1
#endif

/// "sppc_clock_nanosleep" flags.
_gnu_inline _gnu_nonnull(2)
int _sppc_to_os_timer_flags(const int flags, int *_sppc_restrict out) {
  static const _sppc_pair table[] = {_SPPC_PAIR(TIMER_ABSTIME)};
  return _sppc_to_os_flags(flags, table, _SPPC_COUNT(table), out);
}

// ---------- processes ----------

/// A signal; 0 sends none, and only checks the process exists.
_gnu_inline _gnu_nonnull(2)
int _sppc_to_os_signal(const int signal, int *_sppc_restrict out) {
  static const _sppc_pair table[] = {
    {0, 0},
    _SPPC_PAIR(SIGHUP), _SPPC_PAIR(SIGINT), _SPPC_PAIR(SIGQUIT), _SPPC_PAIR(SIGILL), _SPPC_PAIR(SIGTRAP),
    _SPPC_PAIR(SIGABRT), _SPPC_PAIR(SIGBUS), _SPPC_PAIR(SIGFPE), _SPPC_PAIR(SIGKILL), _SPPC_PAIR(SIGUSR1),
    _SPPC_PAIR(SIGSEGV), _SPPC_PAIR(SIGUSR2), _SPPC_PAIR(SIGPIPE), _SPPC_PAIR(SIGALRM), _SPPC_PAIR(SIGTERM),
    _SPPC_PAIR(SIGCHLD), _SPPC_PAIR(SIGCONT), _SPPC_PAIR(SIGSTOP), _SPPC_PAIR(SIGTSTP), _SPPC_PAIR(SIGTTIN),
    _SPPC_PAIR(SIGTTOU), _SPPC_PAIR(SIGURG), _SPPC_PAIR(SIGXCPU), _SPPC_PAIR(SIGXFSZ), _SPPC_PAIR(SIGVTALRM),
    _SPPC_PAIR(SIGPROF), _SPPC_PAIR(SIGWINCH), _SPPC_PAIR(SIGIO), _SPPC_PAIR(SIGSYS),
  };
  return _sppc_to_os_value(signal, table, _SPPC_COUNT(table), out);
}

// ==================== SPLIT STACK ====================

// The top of this thread's unsafe stack. Every function the compiler split,
// reads this in its prologue and writes it back in its epilogue, so it is
// the one piece of the safe-stack scheme that lives out here rather than in
// the generated code. The name is llvm's, not s++'s. [DON'T CHANGE].
extern _sppc_thread_local void *__safestack_unsafe_stack_ptr;

// The mapping behind it, and its size, kept so the thread can hand it back.
extern _sppc_thread_local void *_unsafe_stack_base;
extern _sppc_thread_local size_t _unsafe_stack_size;

// Whether the program was built with split stacks at all. Set by the main
// thread before any other is created, and only read after that, so it needs
// no synchronisation.
extern bool _unsafe_stack_wanted;

#define UNSAFE_STACK_MIN (1024 * 1024)
#define UNSAFE_STACK_MAX (64 * 1024 * 1024)

_gnu_inline _gnu_cold
_sppc_api int sppc_unsafe_stack_up(void) {
  if (_unsafe_stack_base != NULL) { return 0; }
  _unsafe_stack_wanted = true;

  // Sized off the safe stack this thread was actually given, so the two
  // run out at roughly the same depth and neither becomes a surprise limit
  // on the other. A thread whose attributes cannot be read gets the floor.
  size_t size = (size_t)0;
#if SPPC_KERNEL_LINUX
  pthread_attr_t attr;
  if (pthread_getattr_np(pthread_self(), &attr) == 0) {
    void *unused_base;
    pthread_attr_getstack(&attr, &unused_base, &size);
    pthread_attr_destroy(&attr);
  }
#elif SPPC_KERNEL_FREEBSD
  pthread_attr_t attr;
  if (pthread_attr_init(&attr) == 0) {
    if (pthread_attr_get_np(pthread_self(), &attr) == 0) {
      void *unused_base;
      pthread_attr_getstack(&attr, &unused_base, &size);
    }
    pthread_attr_destroy(&attr);
  }
#elif SPPC_KERNEL_DARWIN
  size = pthread_get_stacksize_np(pthread_self());
#endif

  // Clamp the size around the provided minimum and maximum stack sizes,
  // to stay with in spec.
  if (size < UNSAFE_STACK_MIN) { size = UNSAFE_STACK_MIN; }
  if (size > UNSAFE_STACK_MAX) { size = UNSAFE_STACK_MAX; }

  const size_t page = (size_t)sysconf(_SC_PAGESIZE);
  size = (size + page - 1) & ~(page - 1);
  const size_t total = size + page;

  // "MAP_NORESERVE" because this is a stack: the whole of it is reserved
  // address space and only the part actually reached is ever paid for.
  void *const p = mmap(
    NULL, total, PROT_READ | PROT_WRITE,
    MAP_PRIVATE | MAP_ANONYMOUS | _SPPC_MAP_NORESERVE | _SPPC_MAP_STACK, -1, 0);
  if (p == MAP_FAILED) { return _sppc_errno(errno); }

  // The guard goes at the low end, which is the end the stack grows
  // towards, so an overflow of a buffer lands on nothing mapped instead of
  // on whatever the allocator happened to put below it.
  if (mprotect(p, page, PROT_NONE) != 0) {
    const int err = errno;
    munmap(p, total);
    return _sppc_errno(err);
  }

  _unsafe_stack_base = p;
  _unsafe_stack_size = total;
  __safestack_unsafe_stack_ptr = (char*)p + total;
  return 0;
}

_gnu_inline _gnu_cold
_sppc_api void sppc_unsafe_stack_down(void) {
  if (_unsafe_stack_base == NULL) { return; }
  munmap(_unsafe_stack_base, _unsafe_stack_size);
  _unsafe_stack_base = NULL;
  _unsafe_stack_size = 0;
  __safestack_unsafe_stack_ptr = NULL;
}

// ==================== BOOT CODE ====================

_gnu_inline _gnu_cold
_sppc_api int sppc_init(void) {
  signal(SIGPIPE, SIG_IGN); // let write() return EPIPE instead of killing the process when writing to a closed fd.
  signal(SIGCHLD, SIG_DFL); // ensure zombie reaping works correctly.
  signal(SIGHUP, SIG_IGN); // ignore SIGHUP to prevent accidental termination when the controlling terminal is closed.
  _sppc_bt_install(); // print a backtrace on a fatal signal (abort, trap, fault).
  setlocale(LC_ALL, "en_GB.UTF-8");
  tzset();
  pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
#if defined(__GLIBC__)
  mallopt(M_TRIM_THRESHOLD, 128 * 1024); // trim after 128KB free
  mallopt(M_MMAP_THRESHOLD, 64 * 1024); // mmap allocations above 64KB
#endif
  _gt_init(); // initialize the async green thread runtime.

  _return_if_pthread_err(pthread_mutex_init(&_stdin_mutex, NULL))
  _return_if_pthread_err(pthread_mutex_init(&_stdout_mutex, NULL))
  _return_if_pthread_err(pthread_mutex_init(&_stderr_mutex, NULL))
  return 0;
}

_gnu_inline _gnu_cold
_sppc_api int sppc_cleanup(void) {
  // Let every task that is still runnable finish, so one that was spawned and
  // never awaited still releases what it captured rather than being abandoned
  // with the process.
  _gt_drain();

  _return_if_pthread_err(pthread_mutex_destroy(&_stdin_mutex))
  _return_if_pthread_err(pthread_mutex_destroy(&_stdout_mutex))
  _return_if_pthread_err(pthread_mutex_destroy(&_stderr_mutex))
  return 0;
}

// ==================== HEAP ALLOCATION ====================

_gnu_inline _gnu_hot _gnu_malloc _gnu_alloc_size(1)
_sppc_api void* sppc_malloc(const size_t size) {
  _extract_err(void*) malloc(size);
  _return_pointer
}

_gnu_inline _gnu_malloc _gnu_alloc_size(1) _gnu_alloc_align(2)
_sppc_api void* sppc_aligned_alloc(const size_t size, const size_t alignment) {
  // The same answer from every C library, and Windows': any power of two is
  // an alignment, anything else EINVAL (musl takes 0); one below a pointer's
  // size is met by a pointer's, which "posix_memalign" asks for at least; and
  // the size need not be a multiple of it (C11's "aligned_alloc" may refuse
  // one that is not, as macOS's does). "posix_memalign" returns its error
  // rather than setting errno, so it is set here.
  if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
    errno = EINVAL;
    return NULL;
  }
  void *out = NULL;
  const int err = posix_memalign(&out, alignment < sizeof(void*) ? sizeof(void*) : alignment, size);
  if (_sppc_unlikely(err != 0)) {
    errno = err;
    return NULL;
  }
  return out;
}

_gnu_inline _gnu_hot _gnu_malloc _gnu_alloc_size(1, 2)
_sppc_api void* sppc_calloc(const size_t num, const size_t size) {
  _extract_err(void*) calloc(num, size);
  _return_pointer
}

_gnu_inline _gnu_hot _gnu_alloc_size(2)
_sppc_api void* sppc_realloc(void *ptr, const size_t new_size) {
  // A size of 0 frees the block and gives NULL, from every C library and
  // Windows: glibc's answer, where musl's and macOS's keep a block of their
  // own. Of NULL it is a fresh block, as everywhere.
  if (new_size == 0 && ptr != NULL) {
    free(ptr);
    return NULL;
  }
  _extract_err(void*) realloc(ptr, new_size);
  _return_pointer
}

_gnu_inline _gnu_hot
_sppc_api void sppc_free(void *ptr) {
  free(ptr);
}

// The mem* and str* helpers are every platform's (platform/common.h); these
// are the calls they use that are not the same everywhere.
#define _sppc_memmem memmem
#define _sppc_strcasecmp strcasecmp
#define _sppc_strcasestr strcasestr

// ==================== SYSCALLS ====================

_posix_syscall(0)
_gnu_inline _gnu_fd_arg_read(4) _gnu_restrict_access(write_only, 1) _gnu_restrict_access(write_only, 5)
_gnu_nonnull(1, 5)
_sppc_api int sppc_read(char *_sppc_restrict buffer, const size_t size, const size_t count, const int fd,
  ssize_t *_sppc_restrict out_n) {
  _extract_err_async(ssize_t, _gt_try_read(fd, buffer, size * count, &_async_res), read(fd, buffer, size * count))
  _sret_normalised_store(out_n)
  _return_normalized_err
}

_posix_syscall(1)
_gnu_inline _gnu_fd_arg_write(4) _gnu_restrict_access(read_only, 1) _gnu_restrict_access(write_only, 5)
_gnu_nonnull(1, 5)
_sppc_api int sppc_write(char const *_sppc_restrict buffer, const size_t size, const size_t count, const int fd,
  ssize_t *_sppc_restrict out_n) {
  _extract_err_async(ssize_t, _gt_try_write(fd, buffer, size * count, &_async_res), write(fd, buffer, size * count))
  _sret_normalised_store(out_n)
  _return_normalized_err
}

_posix_syscall(2)
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(write_only, 4) _gnu_nonnull(1, 4)
_sppc_api int sppc_open(char const *_sppc_restrict path, const int flags, const uint32_t mode, int *_sppc_restrict out_fd) {
  int os_flags;
  mode_t os_mode;
  if (_sppc_to_os_open_flags(flags, &os_flags) != 0 || _sppc_to_os_mode(mode, &os_mode) != 0) {
    *out_fd = -1;
    return SPPC_EINVAL;
  }
  _extract_err(int) open(path, os_flags, os_mode);
  _sret_normalised_store(out_fd)
#if SPPC_KERNEL_FREEBSD
  // FreeBSD refuses to follow a link with EMLINK, where POSIX (and so sppc)
  // has ELOOP; EMLINK is for a file with too many links.
  if (err != 0 && errno == EMLINK && (os_flags & O_NOFOLLOW) != 0) { return SPPC_ELOOP; }
#endif
  _return_normalized_err
}

_posix_syscall(3)
_gnu_inline _gnu_fd_arg(1)
_sppc_api int sppc_close(const int fd) {
  _extract_err(int) close(fd);
  _return_normalized_err
}

_posix_syscall(4)
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(write_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_stat(char const *_sppc_restrict path, struct sppc_stat *_sppc_restrict out) {
  struct stat st;
  _extract_err(int) stat(path, &st);
  if (err == 0) { _sppc_from_os_stat(&st, out); }
  _return_normalized_err
}

_posix_syscall(5)
_gnu_inline _gnu_fd_arg(1) _gnu_restrict_access(write_only, 2) _gnu_nonnull(2)
_sppc_api int sppc_fstat(const int fd, struct sppc_stat *_sppc_restrict out) {
  struct stat st;
  _extract_err(int) fstat(fd, &st);
  if (err == 0) { _sppc_from_os_stat(&st, out); }
  _return_normalized_err
}

_posix_syscall(6)
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(write_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_lstat(char const *_sppc_restrict path, struct sppc_stat *_sppc_restrict out) {
  struct stat st;
  _extract_err(int) lstat(path, &st);
  if (err == 0) { _sppc_from_os_stat(&st, out); }
  _return_normalized_err
}

/// "sppc_poll" where the platform's event values are not sppc's: a translated
/// copy of the array, on the stack when it is small. Separate so a platform
/// whose values match can still have it tested.
_gnu_inline _gnu_nonnull(1, 4)
int _sppc_poll_translated(sppc_pollfd *_sppc_restrict fds, const uint64_t count, const int timeout, int *_sppc_restrict out_n) {
  size_t n;
  _sppc_pair const *const table = _sppc_poll_table(&n);
  struct pollfd small[64];
  struct pollfd *const heap = count <= 64 ? NULL : (struct pollfd*)malloc(count * sizeof *heap);
  struct pollfd *const os_fds = count <= 64 ? small : heap;
  if (os_fds == NULL) {
    *out_n = -1;
    return SPPC_ENOMEM;
  }
  for (uint64_t i = 0; i < count; ++i) {
    int events = 0;
    if (_sppc_to_os_flags(fds[i].events, table, n, &events) != 0) {
      free(heap);
      *out_n = -1;
      return SPPC_EINVAL;
    }
    os_fds[i] = (struct pollfd){.fd = fds[i].fd, .events = (short)events, .revents = 0};
  }
  const int err = poll(os_fds, (nfds_t)count, timeout);
  const int saved = errno;
  for (uint64_t i = 0; err >= 0 && i < count; ++i) {
    fds[i].revents = (int16_t)_sppc_from_os_flags(os_fds[i].revents, table, n);
  }
  free(heap);
  errno = saved;
  _sret_normalised_store(out_n)
  _return_normalized_err
}

_posix_syscall(7)
_gnu_inline _gnu_restrict_access(read_write, 1) _gnu_restrict_access(write_only, 4) _gnu_nonnull(1, 4)
_sppc_api int sppc_poll(sppc_pollfd *_sppc_restrict fds, const uint64_t count, const int timeout, int *_sppc_restrict out_n) {
  size_t n;
  _sppc_pair const *const table = _sppc_poll_table(&n);
  for (uint64_t i = 0; i < count; ++i) {
    int unused;
    if (_sppc_to_os_flags(fds[i].events, table, n, &unused) != 0) {
      *out_n = -1;
      return SPPC_EINVAL;
    }
  }

  // Where the platform's event values are sppc's, the array goes to the
  // platform as it is: its layout is the platform's own (checked above).
  if (_sppc_table_is_identity(table, n)) {
    _extract_err(int) poll((struct pollfd*)fds, (nfds_t)count, timeout);
    _sret_normalised_store(out_n)
    _return_normalized_err
  }

  return _sppc_poll_translated(fds, count, timeout, out_n);
}

_posix_syscall(8)
_gnu_inline _gnu_fd_arg(1) _gnu_restrict_access(write_only, 4) _gnu_nonnull(4)
_sppc_api int sppc_lseek(const int fd, const int64_t offset, const int whence, int64_t *_sppc_restrict out_pos) {
  if (whence != SPPC_SEEK_SET && whence != SPPC_SEEK_CUR && whence != SPPC_SEEK_END) {
    *out_pos = -1;
    return SPPC_EINVAL;
  }
  _extract_err(off_t) lseek(fd, (off_t)offset, whence);
  _sret_normalised_store(out_pos)
  _return_normalized_err
}

_posix_syscall(9)
_gnu_inline _gnu_malloc _gnu_alloc_size(2) // no fd_arg: MAP_ANONYMOUS passes -1
_sppc_api void* sppc_mmap(const int fd, const size_t length, const int prot, const int flags, const int64_t offset) {
  // FreeBSD maps memory neither shared nor private (as private), and an offset
  // off a page boundary (rounded down, the pointer moved up to it), which POSIX
  // and Linux refuse.
  int os_flags;
  const int sharing = flags & (SPPC_MAP_SHARED | SPPC_MAP_PRIVATE);
  if ((prot & ~_SPPC_PROT_ALL) != 0 || _sppc_to_os_map_flags(flags, &os_flags) != 0
    || (sharing != SPPC_MAP_SHARED && sharing != SPPC_MAP_PRIVATE)
    || offset < 0 || (uint64_t)offset % (uint64_t)sysconf(_SC_PAGESIZE) != 0) { return NULL; }
  _extract_err(void*) mmap(NULL, length, prot, os_flags, fd, (off_t)offset);
  _return_map_pointer
}

#if SPPC_KERNEL_FREEBSD
/// SPPC_ENOMEM if any page in [addr, addr + size) is unmapped, else 0.
/// FreeBSD's "mincore" refuses a range that starts outside a mapping or has
/// a gap in it, and changes nothing; it is asked a stack buffer at a time.
_gnu_inline
int _sppc_freebsd_unmapped(void *const addr, const size_t size) {
  char vec[1024];
  const uintptr_t page = (uintptr_t)sysconf(_SC_PAGESIZE);
  const uintptr_t chunk = sizeof vec * page;
  const uintptr_t start = (uintptr_t)addr;
  if (size > UINTPTR_MAX - start) { return SPPC_ENOMEM; }
  const uintptr_t end = start + size;
  for (uintptr_t at = start; at < end; at += chunk) {
    const size_t n = end - at < chunk ? end - at : chunk;
    if (mincore((void*)at, n, vec) != 0) { return _sppc_errno(errno); }
    if (n < chunk) { break; }
  }
  return 0;
}
#endif

_posix_syscall(10)
_gnu_inline _gnu_nonnull(1)
_sppc_api int sppc_memprotect(void *addr, const size_t size, const int prot) {
  // POSIX refuses an address off a page boundary; musl's "mprotect" rounds
  // it down instead, and would change the page before it.
  if ((prot & ~_SPPC_PROT_ALL) != 0 || (uintptr_t)addr % (uintptr_t)sysconf(_SC_PAGESIZE) != 0) {
    return SPPC_EINVAL;
  }
#if SPPC_KERNEL_FREEBSD
  // FreeBSD's "mprotect" changes what is mapped in the range and passes over
  // what is not; POSIX and Linux refuse a range with unmapped pages in it.
  const int unmapped = _sppc_freebsd_unmapped(addr, size);
  if (unmapped != 0) { return unmapped; }
#endif
  _extract_err(int) mprotect(addr, size, prot);
  _return_normalized_err
}

_posix_syscall(11)
_gnu_inline _gnu_restrict_access(read_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_munmap(void *_sppc_restrict addr, size_t const *_sppc_restrict length) {
  // As for "sppc_memprotect": FreeBSD's "munmap" rounds an address off a page
  // boundary down, and would unmap the page before it.
  if ((uintptr_t)addr % (uintptr_t)sysconf(_SC_PAGESIZE) != 0) { return SPPC_EINVAL; }
  _extract_err(int) munmap(addr, *length);
  _return_normalized_err
}

_posix_syscall(17)
_gnu_inline _gnu_fd_arg_read(1) _gnu_restrict_access(write_only, 2) _gnu_restrict_access(write_only, 6)
_gnu_nonnull(2, 6)
_sppc_api int sppc_pread(const int fd, void *_sppc_restrict buffer, const size_t size, const size_t count, const int64_t offset,
  ssize_t *_sppc_restrict out_n) {
  _extract_err(ssize_t) pread(fd, buffer, size * count, (off_t)offset);
  _sret_normalised_store(out_n)
  _return_normalized_err
}

_posix_syscall(18)
_gnu_inline _gnu_fd_arg_write(1) _gnu_restrict_access(read_only, 2) _gnu_restrict_access(write_only, 6)
_gnu_nonnull(2, 6)
_sppc_api int sppc_pwrite(const int fd, void const *_sppc_restrict buffer, const size_t size, const size_t count,
  const int64_t offset, ssize_t *_sppc_restrict out_n) {
  _extract_err(ssize_t) pwrite(fd, buffer, size * count, (off_t)offset);
  _sret_normalised_store(out_n)
  _return_normalized_err
}

_posix_syscall(19)
_gnu_inline _gnu_fd_arg_read(1) _gnu_restrict_access(read_only, 2) _gnu_restrict_access(write_only, 4)
_gnu_nonnull(2, 4)
_sppc_api int sppc_readv(const int fd, sppc_iovec const *_sppc_restrict iov, const int iov_count, ssize_t *_sppc_restrict out_n) {
  _extract_err(ssize_t) readv(fd, (struct iovec const*)iov, iov_count);
  _sret_normalised_store(out_n)
  _return_normalized_err
}

_posix_syscall(20)
_gnu_inline _gnu_fd_arg_write(1) _gnu_restrict_access(read_only, 2) _gnu_restrict_access(write_only, 4)
_gnu_nonnull(2, 4)
_sppc_api int sppc_writev(const int fd, sppc_iovec const *_sppc_restrict iov, const int iov_count,
  ssize_t *_sppc_restrict out_n) {
  _extract_err(ssize_t) writev(fd, (struct iovec const*)iov, iov_count);
  _sret_normalised_store(out_n)
  _return_normalized_err
}

_posix_syscall(21)
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(write_only, 3) _gnu_nonnull(1, 3)
_sppc_api int sppc_access(char const *_sppc_restrict path, const int flags, int *_sppc_restrict out) {
  if ((flags & ~_SPPC_ACCESS_MODES) != 0) {
    *out = -1;
    return SPPC_EINVAL;
  }
  _extract_err(int) access(path, flags);
  _sret_normalised_store(out)
  _return_normalized_err
}

_posix_syscall(23)
/// sppc's descriptor sets as the platform's, in place, and back again: the
/// same swap. Descriptor n is bit (n % 64) of sppc's 64-bit word n / 64, and
/// of the platform's "long" n / (8 * sizeof(long)), so the bytes are the same
/// but where a long is 32 bits and big-endian: there each word's two halves
/// are the other way round.
_gnu_inline
void _sppc_fd_sets_to_os(sppc_fd_set *const sets[3]) {
#if SPPC_ENDIAN_BIG && ULONG_MAX == 0xFFFFFFFFUL
  for (int s = 0; s < 3; ++s) {
    if (sets[s] == NULL) { continue; }
    for (size_t i = 0; i < _SPPC_COUNT(sets[s]->bits); ++i) {
      sets[s]->bits[i] = sets[s]->bits[i] << 32 | sets[s]->bits[i] >> 32;
    }
  }
#else
  (void)sets;
#endif
}

_gnu_inline _gnu_restrict_access(read_write, 2) _gnu_restrict_access(read_write, 3)
_gnu_restrict_access(read_write, 4) _gnu_restrict_access(read_write, 5)
_gnu_restrict_access(write_only, 6) _gnu_nonnull(6)
_sppc_api int sppc_select(const int nfds, sppc_fd_set *_sppc_restrict readfds, sppc_fd_set *_sppc_restrict writefds,
  sppc_fd_set *_sppc_restrict exceptfds, sppc_timeval *_sppc_restrict timeout, int *_sppc_restrict out_n) {
  // The sets are the platform's own bitmaps (checked above); a descriptor
  // past them would be read and written out of bounds.
  struct timeval tv;
  if (nfds < 0 || nfds > FD_SETSIZE || (timeout != NULL && _sppc_to_os_timeval(timeout, &tv) != 0)) {
    *out_n = -1;
    return SPPC_EINVAL;
  }
  sppc_fd_set *const sets[] = {readfds, writefds, exceptfds};
  _sppc_fd_sets_to_os(sets);
  _extract_err(int) select(nfds, (fd_set*)readfds, (fd_set*)writefds, (fd_set*)exceptfds, timeout ? &tv : NULL);
  _sppc_fd_sets_to_os(sets); // the same swap back
  if (timeout != NULL) { _sppc_from_os_timeval(&tv, timeout); } // Linux leaves the time left in it.
  _sret_normalised_store(out_n)
  _return_normalized_err
}

_posix_syscall(24)
_gnu_inline
_sppc_api void sppc_sched_yield(void) {
  sched_yield();
}

_posix_syscall(26)
_gnu_inline _gnu_restrict_access(read_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_msync(void *_sppc_restrict addr, size_t const *_sppc_restrict length, const int flags) {
  // FreeBSD's MS_SYNC is 0, so it takes both together as MS_ASYNC alone.
  int os_flags;
  if (_sppc_to_os_msync_flags(flags, &os_flags) != 0
    || (flags & (SPPC_MS_SYNC | SPPC_MS_ASYNC)) == (SPPC_MS_SYNC | SPPC_MS_ASYNC)) { return SPPC_EINVAL; }
  _extract_err(int) msync(addr, *length, os_flags);
  _return_normalized_err
}

_posix_syscall(28)
_gnu_inline _gnu_restrict_access(read_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_madvise(void *_sppc_restrict addr, size_t const *_sppc_restrict length, const int advice) {
  int os_advice;
  if (_sppc_to_os_madvise(advice, &os_advice) != 0) { return SPPC_EINVAL; }
#if SPPC_KERNEL_DARWIN
  if (advice == SPPC_MADV_DONTNEED) { return _sppc_madvise_dontneed(addr, *length); } // Linux's, not a hint
#elif SPPC_KERNEL_FREEBSD
  // FreeBSD's is a hint, and its map cannot tell private anonymous memory
  // (which Linux's empties) from shared, so Linux's cannot be built on it.
  if (advice == SPPC_MADV_DONTNEED) { return SPPC_ENOSYS; }
#endif
  _extract_err(int) madvise(addr, *length, os_advice);
  _return_normalized_err
}

_posix_syscall(33)
_gnu_inline _gnu_fd_arg(1) _gnu_restrict_access(write_only, 2) _gnu_nonnull(2)
_sppc_api int sppc_dup(const int fd, int *_sppc_restrict out_fd) {
  _extract_err(int) dup(fd);
  _sret_normalised_store(out_fd)
  _return_normalized_err
}

_posix_syscall(34)
_gnu_inline _gnu_fd_arg(1)
_sppc_api int sppc_dup2(const int fd, const int target_fd) {
  _extract_err(int) dup2(fd, target_fd);
  _return_normalized_err
}

_posix_syscall(39)
_gnu_inline _gnu_restrict_access(write_only, 1) _gnu_nonnull(1)
_sppc_api void sppc_get_pid(int32_t *_sppc_restrict out_pid) {
  *out_pid = (int32_t)getpid();
}

/// "socket", taking the stand-in flags of "_sppc_to_os_sock_type" where the
/// platform's own does not take the real ones.
_gnu_inline
int _sppc_os_socket(const int domain, const int type, const int protocol) {
#if _SPPC_SOCK_FLAGS_BY_FCNTL
  const int flags = type & (_SPPC_OS_SOCK_NONBLOCK | _SPPC_OS_SOCK_CLOEXEC);
  const int fd = socket(domain, type & ~flags, protocol);
  if (fd < 0) { return -1; }
  if (_sppc_fd_set_flags(fd, (flags & _SPPC_OS_SOCK_CLOEXEC) != 0, (flags & _SPPC_OS_SOCK_NONBLOCK) != 0) != 0) {
    const int err = errno;
    close(fd);
    errno = err;
    return -1;
  }
  return fd;
#else
  return socket(domain, type, protocol);
#endif
}

_posix_syscall(41)
_gnu_inline _gnu_restrict_access(write_only, 4) _gnu_nonnull(4)
_sppc_api int sppc_socket(const int domain, const int type, const int protocol, int *_sppc_restrict out_fd) {
  size_t n;
  _sppc_pair const *const families = _sppc_af_table(&n);
  int os_domain, os_type;
  if (_sppc_to_os_value(domain, families, n, &os_domain) != 0 || _sppc_to_os_sock_type(type, &os_type) != 0) {
    *out_fd = -1;
    return SPPC_EINVAL;
  }
  _extract_err(int) _sppc_os_socket(os_domain, os_type, protocol);
  _sret_normalised_store(out_fd)
  _return_normalized_err
}

_posix_syscall(42)
_gnu_inline _gnu_fd_arg(1) _gnu_restrict_access(read_only, 2) _gnu_nonnull(2)
_sppc_api int sppc_connect(const int fd, sppc_sockaddr_storage const *_sppc_restrict storage) {
  struct sockaddr_storage const *const ss = _sppc_os_sockaddr_const(storage);
  const socklen_t len = _sppc_sockaddr_len(ss);
  _extract_err_async(int, _gt_try_connect(fd, (struct sockaddr const*)ss, len, &_async_res), connect(fd, (struct sockaddr const*)ss, len))
  _return_normalized_err
}

_posix_syscall(44)
_gnu_inline _gnu_fd_arg_write(1) _gnu_restrict_access(read_only, 2) _gnu_restrict_access(read_only, 4)
_gnu_restrict_access(write_only, 5) _gnu_nonnull(2, 4, 5)
_sppc_api int sppc_sendto(const int fd, char const *data, const size_t size,
  sppc_sockaddr_storage const *_sppc_restrict storage, ssize_t *_sppc_restrict out_n) {
  struct sockaddr_storage const *const ss = _sppc_os_sockaddr_const(storage);
  _extract_err(ssize_t) sendto(fd, data, size, 0, (struct sockaddr const*)ss, _sppc_sockaddr_len(ss));
  _sret_normalised_store(out_n)
  _return_normalized_err
}

_posix_syscall(45)
_gnu_inline _gnu_fd_arg_read(1) _gnu_restrict_access(write_only, 2) _gnu_restrict_access(write_only, 4)
_gnu_restrict_access(write_only, 5) _gnu_nonnull(2, 4, 5)
_sppc_api int sppc_recvfrom(const int fd, char *buffer, const size_t size,
  sppc_sockaddr_storage *_sppc_restrict out_storage, ssize_t *_sppc_restrict out_n) {
  socklen_t len = (socklen_t)sizeof(struct sockaddr_storage);
  _extract_err(ssize_t) recvfrom(fd, buffer, size, 0, (struct sockaddr*)_sppc_os_sockaddr(out_storage), &len);
  _sret_normalised_store(out_n)
  _return_normalized_err
}

_posix_fake_syscall("Wrapper around `sendto`, using a nullptr storage (not expressible in S++)")
_gnu_inline _gnu_fd_arg_write(1) _gnu_restrict_access(read_only, 2) _gnu_restrict_access(write_only, 5)
_gnu_nonnull(2, 5)
_sppc_api int sppc_send(const int fd, char const *data, const size_t size, const int flags, ssize_t *_sppc_restrict out_n) {
  int os_flags;
  if (_sppc_to_os_msg_flags(flags, &os_flags) != 0) {
    *out_n = -1;
    return SPPC_EINVAL;
  }
  _extract_err_async(ssize_t, _gt_try_send(fd, data, size, os_flags, &_async_res), send(fd, data, size, os_flags))
  _sret_normalised_store(out_n)
  _return_normalized_err
}

_posix_fake_syscall("Wrapper around `recvfrom`, using a nullptr storage (not expressible in S++)")
_gnu_inline _gnu_fd_arg_read(1) _gnu_restrict_access(write_only, 2) _gnu_restrict_access(write_only, 5)
_gnu_nonnull(2, 5)
_sppc_api int sppc_recv(const int fd, char *buffer, const size_t size, const int flags, ssize_t *_sppc_restrict out_n) {
  int os_flags;
  if (_sppc_to_os_msg_flags(flags, &os_flags) != 0) {
    *out_n = -1;
    return SPPC_EINVAL;
  }
  _extract_err_async(ssize_t, _gt_try_recv(fd, buffer, size, os_flags, &_async_res), recv(fd, buffer, size, os_flags))
  _sret_normalised_store(out_n)
  _return_normalized_err
}

_posix_syscall(48)
_gnu_inline _gnu_fd_arg(1)
_sppc_api int sppc_shutdown(const int fd, const int how) {
  if (how != SPPC_SHUT_RD && how != SPPC_SHUT_WR && how != SPPC_SHUT_RDWR) { return SPPC_EINVAL; }
  _extract_err(int) shutdown(fd, how);
  _return_normalized_err
}

_posix_syscall(49)
_gnu_inline _gnu_fd_arg(1) _gnu_restrict_access(read_only, 2) _gnu_nonnull(2)
_sppc_api int sppc_bind(const int fd, sppc_sockaddr_storage const *_sppc_restrict storage) {
  struct sockaddr_storage const *const ss = _sppc_os_sockaddr_const(storage);
  _extract_err(int) bind(fd, (struct sockaddr const*)ss, _sppc_sockaddr_len(ss));
  _return_normalized_err
}

_posix_syscall(50)
_gnu_inline _gnu_fd_arg(1)
_sppc_api int sppc_listen(const int fd, const int backlog) {
  _extract_err(int) listen(fd, backlog);
  _return_normalized_err
}

_posix_syscall(51)
_gnu_inline _gnu_fd_arg(1) _gnu_restrict_access(write_only, 2) _gnu_nonnull(2)
_sppc_api int sppc_getsockname(const int fd, sppc_sockaddr_storage *_sppc_restrict out_storage) {
  socklen_t len = (socklen_t)sizeof(struct sockaddr_storage);
  _extract_err(int) getsockname(fd, (struct sockaddr*)_sppc_os_sockaddr(out_storage), &len);
  _return_normalized_err
}

_gnu_inline _gnu_fd_arg(1) _gnu_restrict_access(write_only, 2) _gnu_nonnull(2)
_sppc_api int sppc_getpeername(const int fd, sppc_sockaddr_storage *_sppc_restrict out_storage) {
  socklen_t len = (socklen_t)sizeof(struct sockaddr_storage);
  _extract_err(int) getpeername(fd, (struct sockaddr*)_sppc_os_sockaddr(out_storage), &len);
  _return_normalized_err
}

_posix_syscall(62)
_gnu_inline
_sppc_api int sppc_signal(const int32_t pid, const int signal) {
  int os_signal;
  if (_sppc_to_os_signal(signal, &os_signal) != 0) { return SPPC_EINVAL; }
  _extract_err(int) kill((pid_t)pid, os_signal);
  _return_normalized_err
}

// F_GETFD (the descriptor flags: SPPC_FD_CLOEXEC), F_GETFL (the file status
// flags: SPPC_O_*), F_GETOWN.
_posix_syscall(72)
_gnu_inline _gnu_fd_arg(1) _gnu_restrict_access(write_only, 3) _gnu_nonnull(3)
_sppc_api int sppc_fcntl_get(const int fd, const int cmd, int *_sppc_restrict out) {
  int os_cmd;
  switch (cmd) {
    case SPPC_F_GETFD: os_cmd = F_GETFD; break;
    case SPPC_F_GETFL: os_cmd = F_GETFL; break;
    case SPPC_F_GETOWN: os_cmd = F_GETOWN; break;
    default: *out = -1; return SPPC_EINVAL;
  }
  _extract_err(int) fcntl(fd, os_cmd);
  _sret_normalised_store(out)
  if (err >= 0 && cmd == SPPC_F_GETFL) { *out = _sppc_from_os_open_flags(err); }
  _return_normalized_err
}

// F_SETFD (the descriptor flags: SPPC_FD_CLOEXEC), F_SETFL (the file status
// flags: SPPC_O_*), F_SETOWN.
_posix_syscall(72)
_gnu_inline _gnu_fd_arg(1)
_sppc_api int sppc_fcntl_set(const int fd, const int cmd, const int arg) {
  int os_cmd, os_arg = arg;
  switch (cmd) {
    case SPPC_F_SETFD:
      if ((arg & ~SPPC_FD_CLOEXEC) != 0) { return SPPC_EINVAL; }
      os_cmd = F_SETFD;
      break;
    case SPPC_F_SETFL:
      if (_sppc_to_os_open_flags(arg, &os_arg) != 0) { return SPPC_EINVAL; }
      os_cmd = F_SETFL;
      break;
    case SPPC_F_SETOWN: os_cmd = F_SETOWN; break;
    default: return SPPC_EINVAL;
  }
  _extract_err(int) fcntl(fd, os_cmd, os_arg);
  _return_normalized_err
}

// F_GETLK, F_SETLK and F_SETLKW. F_GETLK writes back the lock that would
// conflict, or SPPC_F_UNLCK in its type if none would.
_posix_syscall(72)
_gnu_inline _gnu_fd_arg(1) _gnu_restrict_access(read_write, 3) _gnu_nonnull(3)
_sppc_api int sppc_fcntl_lock(const int fd, const int cmd, sppc_flock *_sppc_restrict lock) {
  int os_cmd;
  switch (cmd) {
    case SPPC_F_GETLK: os_cmd = F_GETLK; break;
    case SPPC_F_SETLK: os_cmd = F_SETLK; break;
    case SPPC_F_SETLKW: os_cmd = F_SETLKW; break;
    default: return SPPC_EINVAL;
  }
  size_t n;
  _sppc_pair const *const types = _sppc_lock_types_table(&n);
  int os_type;
  if (_sppc_to_os_value(lock->type, types, n, &os_type) != 0 ||
    (lock->whence != SPPC_SEEK_SET && lock->whence != SPPC_SEEK_CUR && lock->whence != SPPC_SEEK_END)) {
    return SPPC_EINVAL;
  }

  struct flock fl = {0};
  fl.l_type = (short)os_type;
  fl.l_whence = (short)lock->whence;
  fl.l_start = (off_t)lock->start;
  fl.l_len = (off_t)lock->len;
  _extract_err(int) fcntl(fd, os_cmd, &fl);
  if (err == 0 && cmd == SPPC_F_GETLK) {
    lock->type = _sppc_from_os_value(fl.l_type, types, n, SPPC_F_UNLCK);
    lock->whence = (int32_t)fl.l_whence;
    lock->start = (int64_t)fl.l_start;
    lock->len = (int64_t)fl.l_len;
    lock->pid = (int32_t)fl.l_pid;
  }
  _return_normalized_err
}

_posix_syscall(74)
_gnu_inline _gnu_fd_arg(1)
_sppc_api int sppc_fsync(const int fd) {
  _extract_err_async(int, _gt_try_fsync(fd, &_async_res), fsync(fd))
  _return_normalized_err
}

_posix_syscall(76)
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_nonnull(1)
_sppc_api int sppc_truncate(char const *_sppc_restrict path, const int64_t length) {
  _extract_err(int) truncate(path, (off_t)length);
  _return_normalized_err
}

_posix_syscall(77)
_gnu_inline _gnu_fd_arg_write(1)
_sppc_api int sppc_ftruncate(const int fd, const int64_t length) {
  _extract_err(int) ftruncate(fd, (off_t)length);
  _return_normalized_err
}

_posix_syscall(79)
_gnu_inline _gnu_restrict_access(write_only, 1) _gnu_nonnull(1)
_sppc_api int sppc_getcwd(char *_sppc_restrict buffer, const size_t size) {
  _extract_err(char*) getcwd(buffer, size);
  _return_normalized_ptr_err
}

_posix_syscall(80)
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_nonnull(1)
_sppc_api int sppc_chdir(char const *_sppc_restrict path) {
  _extract_err(int) chdir(path);
  _return_normalized_err
}

_posix_syscall(82)
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(read_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_rename(char const *_sppc_restrict old_path, char const *_sppc_restrict new_path) {
  _extract_err(int) rename(old_path, new_path);
  _return_normalized_err
}

_posix_syscall(83)
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_nonnull(1)
_sppc_api int sppc_mkdir(char const *_sppc_restrict path, const uint32_t mode) {
  mode_t os_mode;
  if (_sppc_to_os_mode(mode, &os_mode) != 0) { return SPPC_EINVAL; }
  _extract_err(int) mkdir(path, os_mode);
  _return_normalized_err
}

_posix_syscall(84)
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_nonnull(1)
_sppc_api int sppc_rmdir(char const *_sppc_restrict path) {
  _extract_err(int) rmdir(path);
  _return_normalized_err
}

_posix_syscall(86)
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(read_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_link(char const *_sppc_restrict target, char const *_sppc_restrict linkpath) {
  _extract_err(int) link(target, linkpath);
  _return_normalized_err
}

_posix_syscall(87)
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(read_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_symlink(char const *_sppc_restrict target, char const *_sppc_restrict linkpath) {
  _extract_err(int) symlink(target, linkpath);
  _return_normalized_err
}

_posix_syscall(88)
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_nonnull(1)
_sppc_api int sppc_unlink(char const *_sppc_restrict path) {
  _extract_err(int) unlink(path);
#if SPPC_KERNEL_DARWIN || SPPC_KERNEL_FREEBSD
  // Darwin and FreeBSD refuse a directory with EPERM, which Linux (and so
  // sppc) keeps for a file it may not remove.
  struct stat st;
  if (err != 0 && errno == EPERM && lstat(path, &st) == 0 && S_ISDIR(st.st_mode)) { return SPPC_EISDIR; }
#endif
  _return_normalized_err
}

_posix_syscall(89)
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(write_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_readlink(char const *_sppc_restrict path, char *_sppc_restrict buffer, const size_t buffer_size) {
  // readlink neither terminates nor reports truncation, and its return value
  // is the only record of the length, so both have to be handled here.
  if (buffer_size == 0) { return SPPC_ERANGE; }
  _extract_err(ssize_t) readlink(path, buffer, buffer_size);
  if (err < 0) { return _sppc_errno(errno); }
  if ((size_t)err == buffer_size) {
    *buffer = '\0';
    return SPPC_ERANGE;
  } // no room to terminate
  buffer[err] = '\0';
  return 0;
}

_posix_syscall(90)
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_nonnull(1)
_sppc_api int sppc_chmod(char const *_sppc_restrict path, const uint32_t mode) {
  mode_t os_mode;
  if (_sppc_to_os_mode(mode, &os_mode) != 0) { return SPPC_EINVAL; }
  _extract_err(int) chmod(path, os_mode);
  _return_normalized_err
}

_posix_syscall(92)
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_nonnull(1)
_sppc_api int sppc_chown(char const *_sppc_restrict path, const uint32_t owner, const uint32_t group) {
  _extract_err(int) chown(path, (uid_t)owner, (gid_t)group);
  _return_normalized_err
}

_posix_syscall(94)
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_nonnull(1)
_sppc_api int sppc_lchown(char const *_sppc_restrict path, const uint32_t owner, const uint32_t group) {
  _extract_err(int) lchown(path, (uid_t)owner, (gid_t)group);
  _return_normalized_err
}

_posix_syscall(110)
_gnu_inline _gnu_restrict_access(write_only, 1) _gnu_nonnull(1)
_sppc_api void sppc_get_ppid(int32_t *_sppc_restrict out_ppid) {
  *out_ppid = (int32_t)getppid();
}

_posix_syscall(149)
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_nonnull(1)
_sppc_api int sppc_mlock(const void *addr, const size_t size) {
  _extract_err(int) mlock(addr, size);
  _return_normalized_err
}

_posix_syscall(150)
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_nonnull(1)
_sppc_api int sppc_munlock(const void *addr, const size_t size) {
  _extract_err(int) munlock(addr, size);
  _return_normalized_err
}

_posix_syscall(228)
_gnu_inline _gnu_restrict_access(write_only, 2) _gnu_nonnull(2)
_sppc_api int sppc_clock_gettime(const int clock_id, sppc_timespec *_sppc_restrict out_tp) {
  clockid_t os_clock;
  if (_sppc_to_os_clock(clock_id, &os_clock) != 0) { return SPPC_EINVAL; }
  struct timespec ts;
  _extract_err(int) clock_gettime(os_clock, &ts);
  if (err == 0) { _sppc_from_os_timespec(&ts, out_tp); }
  _return_normalized_err
}

_posix_syscall(280)
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_nonnull(1)
_sppc_api int sppc_utimensat(char const *_sppc_restrict path, const int flags) {
  int os_flags;
  if (_sppc_to_os_at_flags(flags, &os_flags) != 0) { return SPPC_EINVAL; }
  _extract_err(int) utimensat(AT_FDCWD, path, NULL, os_flags);
  _return_normalized_err
}

// ==================== STDLIB ====================

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(write_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_realpath(char const *_sppc_restrict path, char *_sppc_restrict buffer) {
  _extract_err(char*) realpath(path, buffer);
  _return_normalized_ptr_err
}


_gnu_inline _gnu_restrict_access(read_write, 1) _gnu_restrict_access(write_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_mktemp(char *_sppc_restrict path, int *_sppc_restrict out_fd) {
  if (!_sppc_is_temp_template(path)) { *out_fd = -1; return SPPC_EINVAL; }
  _extract_err(int) mkostemp(path, O_CLOEXEC);
  *out_fd = err < 0 ? -1 : err;
  _return_normalized_err
}

_gnu_inline _gnu_restrict_access(read_write, 1) _gnu_nonnull(1)
_sppc_api int sppc_mktemp_dir(char *_sppc_restrict path) {
  if (!_sppc_is_temp_template(path)) { return SPPC_EINVAL; }
  _extract_err(char*) mkdtemp(path);
  _return_normalized_ptr_err
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(write_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_statvfs(char const *_sppc_restrict path, struct sppc_statvfs *_sppc_restrict out) {
  struct statvfs st;
  _extract_err(int) statvfs(path, &st);
  if (err == 0) { _sppc_from_os_statvfs(&st, out); }
  _return_normalized_err
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(write_only, 2, 3) _gnu_nonnull(1, 2)
_sppc_api int sppc_getenv(char const *_sppc_restrict key, char *_sppc_restrict out, const size_t size) {
  if (size == 0) { return SPPC_ERANGE; }
  _extract_err(char*) _sppc_secure_getenv(key);
  if (err == NULL) {
    *out = '\0';
    return SPPC_ENOENT;
  }

  const size_t len = strlen(err);
  if (len >= size) {
    *out = '\0';
    return SPPC_ERANGE;
  }
  memcpy(out, err, len + 1);
  return 0;
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(read_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_setenv(char const *_sppc_restrict key, char const *_sppc_restrict val, const bool overwrite) {
  _extract_err(int) setenv(key, val, overwrite ? 1 : 0);
  _return_normalized_err
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_nonnull(1)
_sppc_api int sppc_unsetenv(char const *_sppc_restrict key) {
  _extract_err(int) unsetenv(key);
  _return_normalized_err
}

// ==================== SAFE SOCKETS ====================

_gnu_inline _gnu_fd_arg(1) _gnu_restrict_access(read_only, 4) _gnu_nonnull(4)
_sppc_api int sppc_setsockopt(const int fd, const int level, const int optname, int const *_sppc_restrict optval) {
  int os_level, os_name;
  _return_if_sppc_err(_sppc_to_os_sockopt(level, optname, &os_level, &os_name))
  _extract_err(int) setsockopt(fd, os_level, os_name, optval, (socklen_t)sizeof(*optval));
  _return_normalized_err
}

_gnu_inline _gnu_fd_arg(1) _gnu_restrict_access(write_only, 4) _gnu_nonnull(4)
_sppc_api int sppc_getsockopt(const int fd, const int level, const int optname, int *_sppc_restrict optval) {
  int os_level, os_name;
  _return_if_sppc_err(_sppc_to_os_sockopt(level, optname, &os_level, &os_name))
  socklen_t optlen = (socklen_t)sizeof(*optval);
  _extract_err(int) getsockopt(fd, os_level, os_name, optval, &optlen);
  // The two options whose value is itself a platform constant.
  if (err == 0 && level == SPPC_SOL_SOCKET && optname == SPPC_SO_ERROR) { *optval = _sppc_errno(*optval); }
  if (err == 0 && level == SPPC_SOL_SOCKET && optname == SPPC_SO_TYPE) {
    size_t n;
    _sppc_pair const *const types = _sppc_sock_types_table(&n);
    *optval = _sppc_from_os_value(*optval, types, n, *optval);
  }
  _return_normalized_err
}

_gnu_inline _gnu_fd_arg(1) _gnu_restrict_access(read_only, 4) _gnu_nonnull(4)
_sppc_api int sppc_setsockopt_timeval(const int fd, const int level, const int optname,
  sppc_timeval const *_sppc_restrict optval) {
  int os_level, os_name;
  _return_if_sppc_err(_sppc_to_os_sockopt(level, optname, &os_level, &os_name))
  struct timeval tv;
  if (_sppc_to_os_timeval(optval, &tv) != 0) { return SPPC_EINVAL; }
  _extract_err(int) setsockopt(fd, os_level, os_name, &tv, (socklen_t)sizeof tv);
  _return_normalized_err
}

_gnu_inline _gnu_fd_arg(1) _gnu_restrict_access(write_only, 4) _gnu_nonnull(4)
_sppc_api int sppc_getsockopt_timeval(const int fd, const int level, const int optname,
  sppc_timeval *_sppc_restrict optval) {
  int os_level, os_name;
  _return_if_sppc_err(_sppc_to_os_sockopt(level, optname, &os_level, &os_name))
  struct timeval tv;
  socklen_t optlen = (socklen_t)sizeof tv;
  _extract_err(int) getsockopt(fd, os_level, os_name, &tv, &optlen);
  if (err == 0) { _sppc_from_os_timeval(&tv, optval); }
  _return_normalized_err
}

_gnu_inline _gnu_fd_arg(1) _gnu_restrict_access(read_only, 4) _gnu_nonnull(4)
_sppc_api int sppc_setsockopt_linger(const int fd, const int level, const int optname,
  sppc_linger const *_sppc_restrict optval) {
  int os_level, os_name;
  _return_if_sppc_err(_sppc_to_os_sockopt(level, optname, &os_level, &os_name))
  const struct linger lg = {.l_onoff = optval->onoff, .l_linger = optval->linger};
  _extract_err(int) setsockopt(fd, os_level, os_name, &lg, (socklen_t)sizeof lg);
  _return_normalized_err
}

_gnu_inline _gnu_fd_arg(1) _gnu_restrict_access(write_only, 4) _gnu_nonnull(4)
_sppc_api int sppc_getsockopt_linger(const int fd, const int level, const int optname,
  sppc_linger *_sppc_restrict optval) {
  int os_level, os_name;
  _return_if_sppc_err(_sppc_to_os_sockopt(level, optname, &os_level, &os_name))
  struct linger lg;
  socklen_t optlen = (socklen_t)sizeof lg;
  _extract_err(int) getsockopt(fd, os_level, os_name, &lg, &optlen);
  if (err == 0) {
    optval->onoff = (int32_t)lg.l_onoff;
    optval->linger = (int32_t)lg.l_linger;
  }
  _return_normalized_err
}

// ==================== LINUX AND FREEBSD ====================

// The calls beyond POSIX that Linux and FreeBSD both have: "pipe2",
// "accept4", "fdatasync", "clock_nanosleep", "getrandom" and
// "copy_file_range". Darwin builds the same functions in platform/darwin.h.

#if !SPPC_KERNEL_DARWIN

#include <sys/random.h>

// ---------- syscalls ----------

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

// ---------- stdlib ----------

_gnu_inline _gnu_fd_arg_read(1) _gnu_fd_arg_write(2) _gnu_restrict_access(write_only, 5) _gnu_nonnull(5)
_sppc_api int sppc_copyfile(const int fd_in, const int fd_out, const size_t len, const int flags,
  ssize_t *_sppc_restrict out_n) {
  // No flags are defined yet; the argument keeps room for them.
  if (flags != 0) {
    *out_n = -1;
    return SPPC_EINVAL;
  }
  // A partial copy is normal, so the count has to reach the caller. Where the kernel refuses to copy between the two
  // files (across filesystems before Linux 5.3 and again for some from 5.19, a kernel without the call, a filesystem
  // that does not support it), the same copy is made by reading and writing.
  ssize_t copied = copy_file_range(fd_in, NULL, fd_out, NULL, len, 0);
  if (copied < 0 && (errno == EXDEV || errno == ENOSYS || errno == EOPNOTSUPP)) {
    copied = _sppc_copy_range(fd_in, fd_out, len);
  }
  _extract_err(ssize_t) copied;
  _sret_normalised_store(out_n)
  _return_normalized_err
}

#endif

// ==================== EVERY PLATFORM'S ====================

#include <sppc/platform/common.h>
