#pragma once

/// The Windows implementation of the public api, in the order its parts
/// need each other: the system headers, the errors Win32 and Winsock report,
/// pthreads and clocks built from Win32 (which the async runtime and its
/// locks stand on), then the runtime and the crash reporter, then the api:
/// boot code, split stacks, the heap, clocks, file descriptors over sppc's
/// own fd table, and finally the parts every platform shares
/// (platform/common.h). The port is under way (PORTING.md section 6);
/// whatever is not here yet is not exported on Windows yet.

#include <sppc/config.h>
#include <sppc/macros.h>
#include <sppc/closure.h>
#include <sppc/types.h>
#include <sppc/errors.h>

#if !SPPC_PLATFORM_WINDOWS
  #error "sppc: platform/windows.h is for Windows"
#endif

// ==================== SYSTEM HEADERS ====================

// Winsock 2 ahead of <windows.h> (whose own <winsock.h> would otherwise be
// pulled in and clash with it), none of <windows.h>'s rarely used parts or
// min/max macros, and the POSIX "ssize_t" msvc lacks.

#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
  #define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <mswsock.h> // AcceptEx and ConnectEx, for the completion port
#include <basetsd.h>

#if defined(_MSC_VER)
  #pragma comment(lib, "ws2_32.lib") // Winsock, started by "sppc_init"
#endif

// mingw's <sys/types.h> (through the CRT headers) has it already.
#if !defined(_SSIZE_T_DEFINED) && !defined(__MINGW32__)
  typedef SSIZE_T ssize_t;
  #define _SSIZE_T_DEFINED
#endif

#include <winioctl.h>
#include <psapi.h> // QueryWorkingSetEx (kernel32's K32 name), for locked pages
#include <bcrypt.h>
#include <process.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>
#include <wctype.h>

#if defined(_MSC_VER)
  #pragma comment(lib, "bcrypt.lib") // the random bytes of temporary names and sppc_getrandom
#endif

// ==================== ERRORS ====================

// The C library's errno goes through sppc/errors.h as on every platform;
// Win32's and Winsock's own codes are translated here.

/// A Win32 error ("GetLastError") as sppc's. Many to one, after the C
/// library's own "_dosmaperr", with a nearer fit where Linux has one (a
/// write-protected disk is EROFS there, not EACCES; a file in use by
/// another process EBUSY). 0 is 0, and anything else is SPPC_EIO.
_gnu_inline
int _sppc_errno_win32(const DWORD err) {
  switch (err) {
    case ERROR_SUCCESS: return 0;
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_DRIVE:
    case ERROR_NO_MORE_FILES:
    case ERROR_BAD_NETPATH:
    case ERROR_BAD_NET_NAME:
    case ERROR_BAD_PATHNAME:
    case ERROR_MOD_NOT_FOUND:
      return SPPC_ENOENT;
    case ERROR_TOO_MANY_OPEN_FILES: return SPPC_EMFILE;
    case ERROR_ACCESS_DENIED:
    case ERROR_CURRENT_DIRECTORY:
    case ERROR_NETWORK_ACCESS_DENIED:
    case ERROR_CANNOT_MAKE:
    case ERROR_FAIL_I24:
    case ERROR_DRIVE_LOCKED:
    case ERROR_SEEK_ON_DEVICE:
    case ERROR_NOT_LOCKED:
    case ERROR_LOCK_FAILED:
    case ERROR_DELETE_PENDING:
      return SPPC_EACCES;
    case ERROR_PRIVILEGE_NOT_HELD:
    case ERROR_NOT_OWNER:
      return SPPC_EPERM;
    case ERROR_INVALID_HANDLE:
    case ERROR_INVALID_TARGET_HANDLE:
    case ERROR_DIRECT_ACCESS_HANDLE:
      return SPPC_EBADF;
    case ERROR_ARENA_TRASHED:
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_INVALID_BLOCK:
    case ERROR_NOT_ENOUGH_QUOTA:
    case ERROR_OUTOFMEMORY:
    case ERROR_COMMITMENT_LIMIT:
    case ERROR_WORKING_SET_QUOTA:
      return SPPC_ENOMEM;
    case ERROR_BAD_ENVIRONMENT: return SPPC_E2BIG;
    case ERROR_BAD_FORMAT:
    case ERROR_BAD_EXE_FORMAT:
    case ERROR_INVALID_EXE_SIGNATURE:
    case ERROR_EXE_MACHINE_TYPE_MISMATCH:
      return SPPC_ENOEXEC;
    case ERROR_INVALID_FUNCTION:
    case ERROR_INVALID_ACCESS:
    case ERROR_INVALID_DATA:
    case ERROR_INVALID_PARAMETER:
    case ERROR_NEGATIVE_SEEK:
    case ERROR_INVALID_NAME:
    case ERROR_NOT_A_REPARSE_POINT:
      return SPPC_EINVAL;
    case ERROR_NOT_SAME_DEVICE: return SPPC_EXDEV;
    case ERROR_WRITE_PROTECT: return SPPC_EROFS;
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
    case ERROR_BUSY:
    case ERROR_PIPE_BUSY:
      return SPPC_EBUSY;
    case ERROR_HANDLE_DISK_FULL:
    case ERROR_DISK_FULL:
      return SPPC_ENOSPC;
    case ERROR_DISK_QUOTA_EXCEEDED: return SPPC_EDQUOT;
    case ERROR_FILE_TOO_LARGE: return SPPC_EFBIG;
    case ERROR_FILE_EXISTS:
    case ERROR_ALREADY_EXISTS:
      return SPPC_EEXIST;
    case ERROR_BROKEN_PIPE:
    case ERROR_NO_DATA:
      return SPPC_EPIPE;
    case ERROR_DIR_NOT_EMPTY: return SPPC_ENOTEMPTY;
    case ERROR_DIRECTORY: return SPPC_ENOTDIR;
    case ERROR_DIRECTORY_NOT_SUPPORTED: return SPPC_EISDIR;
    case ERROR_FILENAME_EXCED_RANGE: return SPPC_ENAMETOOLONG;
    case ERROR_CANT_RESOLVE_FILENAME:
    case ERROR_STOPPED_ON_SYMLINK:
      return SPPC_ELOOP;
    case ERROR_TOO_MANY_LINKS: return SPPC_EMLINK;
    case ERROR_INSUFFICIENT_BUFFER: return SPPC_ERANGE;
    case ERROR_DEV_NOT_EXIST:
    case ERROR_BAD_UNIT:
      return SPPC_ENODEV;
    case ERROR_WAIT_NO_CHILDREN:
    case ERROR_CHILD_NOT_COMPLETE:
      return SPPC_ECHILD;
    case ERROR_NO_PROC_SLOTS:
    case ERROR_MAX_THRDS_REACHED:
    case ERROR_NESTING_NOT_ALLOWED:
      return SPPC_EAGAIN;
    case ERROR_POSSIBLE_DEADLOCK: return SPPC_EDEADLK;
    case ERROR_TIMEOUT:
    case ERROR_SEM_TIMEOUT:
    case WAIT_TIMEOUT:
      return SPPC_ETIMEDOUT;
    case ERROR_OPERATION_ABORTED: return SPPC_ECANCELED;
    case ERROR_NOT_SUPPORTED: return SPPC_EOPNOTSUPP;
    case ERROR_CALL_NOT_IMPLEMENTED: return SPPC_ENOSYS;
    default: return SPPC_EIO;
  }
}

/// A Winsock error ("WSAGetLastError") as sppc's. Winsock took the BSD
/// names, so nearly every one has its namesake; the few left are its own
/// spellings of a Linux error. Its name-resolution errors (WSAHOST_NOT_FOUND
/// and the like) are not errno values on any platform, so are SPPC_EIO
/// here with everything else.
_gnu_inline
int _sppc_errno_wsa(const int err) {
  switch (err) {
    case 0: return 0;
    case WSAEINTR: return SPPC_EINTR;
    case WSAEBADF:
    case WSA_INVALID_HANDLE:
      return SPPC_EBADF;
    case WSAEACCES: return SPPC_EACCES;
    case WSAEFAULT: return SPPC_EFAULT;
    case WSAEINVAL:
    case WSA_INVALID_PARAMETER:
      return SPPC_EINVAL;
    case WSAEMFILE: return SPPC_EMFILE;
    case WSAEWOULDBLOCK: return SPPC_EAGAIN;
    case WSAEINPROGRESS: return SPPC_EINPROGRESS;
    case WSAEALREADY: return SPPC_EALREADY;
    case WSAENOTSOCK: return SPPC_ENOTSOCK;
    case WSAEDESTADDRREQ: return SPPC_EDESTADDRREQ;
    case WSAEMSGSIZE: return SPPC_EMSGSIZE;
    case WSAEPROTOTYPE: return SPPC_EPROTOTYPE;
    case WSAENOPROTOOPT: return SPPC_ENOPROTOOPT;
    case WSAEPROTONOSUPPORT: return SPPC_EPROTONOSUPPORT;
    case WSAESOCKTNOSUPPORT: return SPPC_ESOCKTNOSUPPORT;
    case WSAEOPNOTSUPP: return SPPC_EOPNOTSUPP;
    case WSAEPFNOSUPPORT: return SPPC_EPFNOSUPPORT;
    case WSAEAFNOSUPPORT: return SPPC_EAFNOSUPPORT;
    case WSAEADDRINUSE: return SPPC_EADDRINUSE;
    case WSAEADDRNOTAVAIL: return SPPC_EADDRNOTAVAIL;
    case WSAENETDOWN: return SPPC_ENETDOWN;
    case WSAENETUNREACH: return SPPC_ENETUNREACH;
    case WSAENETRESET: return SPPC_ENETRESET;
    case WSAECONNABORTED: return SPPC_ECONNABORTED;
    case WSAECONNRESET:
    case WSAEDISCON:
      return SPPC_ECONNRESET;
    case WSAENOBUFS: return SPPC_ENOBUFS;
    case WSAEISCONN: return SPPC_EISCONN;
    case WSAENOTCONN: return SPPC_ENOTCONN;
    // Sending after a shutdown: Linux's send says EPIPE, as a write to a closed pipe.
    case WSAESHUTDOWN: return SPPC_EPIPE;
    case WSAETOOMANYREFS: return SPPC_ETOOMANYREFS;
    case WSAETIMEDOUT: return SPPC_ETIMEDOUT;
    case WSAECONNREFUSED: return SPPC_ECONNREFUSED;
    case WSAELOOP: return SPPC_ELOOP;
    case WSAENAMETOOLONG: return SPPC_ENAMETOOLONG;
    case WSAEHOSTDOWN: return SPPC_EHOSTDOWN;
    case WSAEHOSTUNREACH: return SPPC_EHOSTUNREACH;
    case WSAENOTEMPTY: return SPPC_ENOTEMPTY;
    case WSAEPROCLIM: return SPPC_EAGAIN;
    case WSAEUSERS: return SPPC_EUSERS;
    case WSAEDQUOT: return SPPC_EDQUOT;
    case WSAESTALE: return SPPC_ESTALE;
    case WSAEREMOTE: return SPPC_EREMOTE;
    case WSA_NOT_ENOUGH_MEMORY: return SPPC_ENOMEM;
    case WSA_OPERATION_ABORTED:
    case WSAECANCELLED:
      return SPPC_ECANCELED;
    default: return SPPC_EIO;
  }
}

// ==================== PTHREADS ====================

// The part of <pthread.h> sppc is built on (threads, keys, once, mutexes,
// condition variables, rwlocks, spin locks and barriers, with glibc's
// clock-based waits), and the clock calls (time and sleep) their deadlines
// are given in, from Win32 alone. Windows has no <pthread.h>, and mingw's
// (winpthreads) is a separate dll sppc does not ship, so it is refused rather
// than mixed in. Ported from the experimental pthreadwin, with its mutex
// rebuilt (it held a kernel mutex, a system call on every lock and unlock)
// and thread exit moved off fiber-local storage, which the runtime's fibers
// would split.
//
// Every definition has an "_sppc_" name, and the POSIX name is a macro for
// it, so the code shared with the POSIX platforms reads the same here while
// nothing in the library claims a name a later SDK might declare. The names
// platform/posix.h gives glibc's extensions ("_sppc_pthread_mutex_clocklock",
// the spin locks, once) are the functions themselves. Each call returns as
// glibc's does: 0 or an error number, the CRT's (whose ETIMEDOUT is 138,
// EDEADLK 36). The waits sleep on "WaitOnAddress", so lock, unlock, signal
// and wake are a system call only when a thread has to sleep or be woken.

#if defined(WIN_PTHREADS_H)
  #error "sppc: winpthreads' <pthread.h> is included; sppc brings its own pthreads on Windows"
#endif
#if defined(_MSC_VER)
  #pragma comment(lib, "synchronization.lib") // WaitOnAddress and its wakes
#endif

// ==================== PTHREADS: CLOCKS ====================

// mingw's <time.h> declares these (from winpthreads' pthread_time.h)
// unless the build is strict; msvc's never does.
#ifndef __clockid_t_defined
  typedef int clockid_t;
  #define __clockid_t_defined 1
#endif
#ifndef CLOCK_REALTIME
  #define CLOCK_REALTIME 0
#endif
#ifndef CLOCK_MONOTONIC
  #define CLOCK_MONOTONIC 1
#endif
#ifndef CLOCK_PROCESS_CPUTIME_ID
  #define CLOCK_PROCESS_CPUTIME_ID 2
#endif
#ifndef CLOCK_THREAD_CPUTIME_ID
  #define CLOCK_THREAD_CPUTIME_ID 3
#endif
#ifndef TIMER_ABSTIME
  #define TIMER_ABSTIME 1
#endif

/// Seconds between the Windows epoch (1601) and the Unix one, in the
/// 100 ns ticks a FILETIME counts.
#define _SPPC_WIN_EPOCH_TICKS 116444736000000000ULL

_gnu_inline _gnu_nonnull(1)
uint64_t _sppc_win_filetime(FILETIME const *ft) {
  return (uint64_t)ft->dwHighDateTime << 32 | ft->dwLowDateTime;
}

/// The four POSIX clocks, as POSIX's "clock_gettime": 0, or -1 with errno
/// set to EINVAL for any other clock. Real time is the precise system time;
/// monotonic is the performance counter, which never stops or steps; the
/// cpu-time ones are the kernel and user time charged to the process or
/// the calling thread, which Windows counts in scheduler ticks (15.6 ms,
/// usually), so they move in steps.
_gnu_inline _gnu_nonnull(2)
int _sppc_clock_gettime(const clockid_t clock, struct timespec *out) {
  switch (clock) {
    case CLOCK_REALTIME: {
      FILETIME ft;
      GetSystemTimePreciseAsFileTime(&ft);
      const uint64_t ticks = _sppc_win_filetime(&ft) - _SPPC_WIN_EPOCH_TICKS;
      out->tv_sec = (time_t)(ticks / 10000000ULL);
      out->tv_nsec = (long)(ticks % 10000000ULL * 100);
      return 0;
    }
    case CLOCK_PROCESS_CPUTIME_ID:
    case CLOCK_THREAD_CPUTIME_ID: {
      FILETIME created, exited, kernel, user;
      const BOOL ok = clock == CLOCK_PROCESS_CPUTIME_ID
        ? GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)
        : GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user);
      if (!ok) { break; }
      const uint64_t ticks = _sppc_win_filetime(&kernel) + _sppc_win_filetime(&user);
      out->tv_sec = (time_t)(ticks / 10000000ULL);
      out->tv_nsec = (long)(ticks % 10000000ULL * 100);
      return 0;
    }
    case CLOCK_MONOTONIC: {
      LARGE_INTEGER freq, count;
      QueryPerformanceFrequency(&freq);
      QueryPerformanceCounter(&count);
      // The remainder is below the frequency (10 MHz, or a few GHz where
      // it is the TSC), so scaling it to nanoseconds cannot overflow.
      out->tv_sec = (time_t)(count.QuadPart / freq.QuadPart);
      out->tv_nsec = (long)(count.QuadPart % freq.QuadPart * 1000000000LL / freq.QuadPart);
      return 0;
    }
    default:
      break;
  }
  // Zeroed as well, so a caller that ignores the error (the runtime's
  // deadline arithmetic, on a clock it has already checked) reads a time.
  out->tv_sec = 0;
  out->tv_nsec = 0;
  errno = EINVAL;
  return -1;
}

#define clock_gettime _sppc_clock_gettime

/// Sleep for "ticks" (100 ns each, more than 0) on a high-resolution
/// waitable timer: "Sleep" counts whole scheduler ticks (15.6 ms, usually),
/// far coarser than a timespec. A Windows too old for one (before 10 1803)
/// gets a plain timer, and one that cannot have a timer at all, "Sleep".
_gnu_inline
void _sppc_win_sleep_ticks(const LONGLONG ticks) {
  HANDLE timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
  if (timer == NULL) { timer = CreateWaitableTimerExW(NULL, NULL, 0, TIMER_ALL_ACCESS); }
  LARGE_INTEGER due;
  due.QuadPart = -ticks; // negative: relative to now
  if (timer == NULL || !SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE)) {
    Sleep((DWORD)((ticks + 9999) / 10000));
  } else {
    WaitForSingleObject(timer, INFINITE);
  }
  if (timer != NULL) { CloseHandle(timer); }
}

/// POSIX's "clock_nanosleep", returning its error as that does: never ends
/// early, as the clock is read again after each wake and the sleep resumed
/// until the deadline. A relative sleep is timed on the monotonic clock,
/// as POSIX has setting the real-time clock leave relative sleeps alone; an
/// absolute one waits for "clock" itself to reach it. The cpu-time clocks
/// do not move while their thread sleeps, so are refused, as on Darwin.
/// Nothing interrupts it, so "remain" is never written.
_gnu_inline _gnu_nonnull(3)
int _sppc_clock_nanosleep(const clockid_t clock, const int flags, struct timespec const *request,
  struct timespec *remain) {
  (void)remain;
  if (clock != CLOCK_REALTIME && clock != CLOCK_MONOTONIC) { return EINVAL; }
  if (request->tv_nsec < 0 || request->tv_nsec >= 1000000000L || request->tv_sec < 0) { return EINVAL; }
  clockid_t on = clock;
  struct timespec deadline = *request;
  if ((flags & TIMER_ABSTIME) == 0) {
    on = CLOCK_MONOTONIC;
    struct timespec now;
    _sppc_clock_gettime(on, &now);
    deadline.tv_sec += now.tv_sec;
    deadline.tv_nsec += now.tv_nsec;
    if (deadline.tv_nsec >= 1000000000L) {
      deadline.tv_sec += 1;
      deadline.tv_nsec -= 1000000000L;
    }
  }
  for (;;) {
    struct timespec now;
    _sppc_clock_gettime(on, &now);
    const int64_t sec = (int64_t)deadline.tv_sec - (int64_t)now.tv_sec;
    if (sec < 0) { return 0; }
    // A day at most at a time, so the 100 ns count cannot overflow.
    if (sec >= 86400) {
      _sppc_win_sleep_ticks(86400LL * 10000000LL);
      continue;
    }
    const int64_t ns = sec * 1000000000LL + (deadline.tv_nsec - now.tv_nsec);
    if (ns <= 0) { return 0; }
    _sppc_win_sleep_ticks((LONGLONG)((ns + 99) / 100));
  }
}

#define clock_nanosleep _sppc_clock_nanosleep

/// POSIX's "nanosleep": a relative sleep on the monotonic clock, with its
/// error in errno, as that has it.
_gnu_inline _gnu_nonnull(1)
int _sppc_nanosleep(struct timespec const *request, struct timespec *remain) {
  const int err = _sppc_clock_nanosleep(CLOCK_MONOTONIC, 0, request, remain);
  if (err == 0) { return 0; }
  errno = err;
  return -1;
}

#define nanosleep _sppc_nanosleep

// ==================== PTHREADS: WAITING ====================

/// EINVAL unless a wait can be timed against @p clock and @p deadline
/// (absolute, or NULL for none) is a real time.
_gnu_inline
int _sppc_win_check_deadline(const clockid_t clock, struct timespec const *deadline) {
  if (clock != CLOCK_REALTIME && clock != CLOCK_MONOTONIC) { return EINVAL; }
  if (deadline != NULL && (deadline->tv_nsec < 0 || deadline->tv_nsec >= 1000000000L)) { return EINVAL; }
  return 0;
}

/// How long a Win32 wait may sleep before @p deadline on @p clock, into
/// @p out: INFINITE for no deadline, 0 once it has passed, and otherwise
/// rounded up to whole milliseconds, so the wait never ends before it.
/// The longest finite wait (49.7 days) stands in for one further off;
/// the caller just waits again.
_gnu_inline _gnu_nonnull(3)
void _sppc_win_wait_ms(const clockid_t clock, struct timespec const *deadline, DWORD *out) {
  if (deadline == NULL) {
    *out = INFINITE;
    return;
  }
  struct timespec now;
  _sppc_clock_gettime(clock, &now);
  const int64_t sec = (int64_t)deadline->tv_sec - (int64_t)now.tv_sec;
  if (sec < 0) {
    *out = 0;
    return;
  }
  if (sec >= (int64_t)(INFINITE / 1000)) {
    *out = INFINITE - 1;
    return;
  }
  const int64_t ns = sec * 1000000000LL + (deadline->tv_nsec - now.tv_nsec);
  *out = ns <= 0 ? 0 : (DWORD)((ns + 999999) / 1000000);
}

/// Sleep while the @p size bytes at @p addr still equal those at
/// @p expected: until woken, until the value has moved on, or spuriously.
/// ETIMEDOUT, without sleeping, once @p deadline (on a clock already
/// checked) has passed. A sleep that ends at its timeout returns 0, so the
/// caller re-checks what it is waiting for and calls again, and only then
/// sees the deadline gone.
_gnu_inline _gnu_nonnull(1, 2)
int _sppc_win_wait(volatile void *addr, void *expected, const size_t size, const clockid_t clock,
  struct timespec const *deadline) {
  DWORD ms;
  _sppc_win_wait_ms(clock, deadline, &ms);
  if (ms == 0) { return ETIMEDOUT; }
  WaitOnAddress(addr, expected, size, ms);
  return 0;
}

// ==================== PTHREADS: THREADS ====================

#define PTHREAD_KEYS_MAX 128
#define PTHREAD_DESTRUCTOR_ITERATIONS 4
#define PTHREAD_STACK_MIN 16384
#define PTHREAD_CREATE_JOINABLE 0
#define PTHREAD_CREATE_DETACHED 1

typedef DWORD _sppc_pthread_key_t;

typedef struct _sppc_pthread_attr_t {
  int detachstate;
  size_t stacksize;
} _sppc_pthread_attr_t;

/// A key is in use while its sequence number is odd. A thread's value
/// remembers the number it was set under, so after a key is deleted and
/// made again, no thread sees the old key's value through the new one.
typedef struct _sppc_win_key {
  volatile LONG seq;
  void (*destructor)(void*);
} _sppc_win_key;

typedef struct _sppc_win_slot {
  void *value;
  LONG seq;
} _sppc_win_slot;

/// A thread. "refs" counts the thread itself while it runs and the join
/// still owed on it, if any, and the last to let go frees it. A thread sppc
/// did not start (the main one, or any other the host made) gets one on
/// first use, with no handle and no join owed.
typedef struct _sppc_win_thread {
  HANDLE handle;
  void *(*start)(void*);
  void *arg;
  void *retval;
  volatile LONG refs;
  volatile LONG joinable;
  _sppc_win_slot slots[PTHREAD_KEYS_MAX];
} _sppc_win_thread;

typedef _sppc_win_thread *_sppc_pthread_t;

// Defined once, in sppc.c.
extern _sppc_win_key _sppc_win_keys[PTHREAD_KEYS_MAX];
extern SRWLOCK _sppc_win_keys_lock;
extern _sppc_thread_local _sppc_pthread_t _sppc_win_self;

_gnu_inline _gnu_nonnull(1)
void _sppc_win_release(_sppc_win_thread *t) {
  if (InterlockedDecrement(&t->refs) != 0) { return; }
  if (t->handle != NULL) { CloseHandle(t->handle); }
  free(t);
}

/// POSIX's: each value still set whose key has a destructor is cleared,
/// then handed to it, in rounds while destructors keep setting values.
_gnu_inline _gnu_nonnull(1)
void _sppc_win_run_destructors(_sppc_win_thread *t) {
  for (int round = 0; round < PTHREAD_DESTRUCTOR_ITERATIONS; ++round) {
    bool ran = false;
    for (DWORD key = 0; key < PTHREAD_KEYS_MAX; ++key) {
      _sppc_win_slot *const slot = &t->slots[key];
      void *const value = slot->value;
      if (value == NULL) { continue; }
      AcquireSRWLockShared(&_sppc_win_keys_lock);
      const LONG seq = _sppc_win_keys[key].seq;
      void (*const destructor)(void*) = _sppc_win_keys[key].destructor;
      ReleaseSRWLockShared(&_sppc_win_keys_lock);
      slot->value = NULL;
      if (slot->seq == seq && destructor != NULL) {
        destructor(value);
        ran = true;
      }
    }
    if (!ran) { return; }
  }
}

/// A thread's end: its keys' destructors, which may still use it (and set
/// values again), then its own reference let go.
_gnu_inline _gnu_nonnull(1)
void _sppc_win_finish(_sppc_win_thread *t) {
  _sppc_win_run_destructors(t);
  _sppc_win_self = NULL;
  _sppc_win_release(t);
}

/// The calling thread, adopted on first use if sppc did not start it.
/// NULL only if that cannot be allocated.
_gnu_inline
_sppc_pthread_t _sppc_win_current(void) {
  if (_sppc_win_self != NULL) { return _sppc_win_self; }
  _sppc_win_thread *const t = (_sppc_win_thread*)calloc(1, sizeof *t);
  if (t == NULL) { return NULL; }
  t->refs = 1;
  _sppc_win_self = t;
  return t;
}

/// Every thread's exit, through the image's TLS callback (registered in
/// sppc.c), which the loader makes per thread whatever fiber it is on; a
/// fiber-local slot's callback would instead fire when the fiber that set
/// it is deleted. A thread sppc started has finished before it gets here,
/// so this is for the adopted ones. The process's own exit is left alone:
/// POSIX runs no key destructors for the thread that calls "exit".
_gnu_inline_va
void NTAPI _sppc_win_tls_callback(PVOID module, const DWORD reason, PVOID reserved) {
  (void)module;
  (void)reserved;
  if (reason == DLL_THREAD_DETACH && _sppc_win_self != NULL) { _sppc_win_finish(_sppc_win_self); }
}

/// Its keys' destructors run here, on the thread, while the CRT is still
/// up for them; "_beginthreadex" tears it down after the return.
_gnu_inline_va _gnu_nonnull(1)
unsigned __stdcall _sppc_win_thread_start(void *param) {
  _sppc_win_thread *const t = (_sppc_win_thread*)param;
  _sppc_win_self = t;
  t->retval = t->start(t->arg);
  _sppc_win_finish(t);
  return 0;
}

_gnu_inline _gnu_nonnull(1)
int _sppc_pthread_attr_init(_sppc_pthread_attr_t *attr) {
  attr->detachstate = PTHREAD_CREATE_JOINABLE;
  attr->stacksize = 0;
  return 0;
}

_gnu_inline _gnu_nonnull(1)
int _sppc_pthread_attr_destroy(_sppc_pthread_attr_t *attr) {
  (void)attr;
  return 0;
}

_gnu_inline _gnu_nonnull(1)
int _sppc_pthread_attr_setdetachstate(_sppc_pthread_attr_t *attr, const int state) {
  if (state != PTHREAD_CREATE_JOINABLE && state != PTHREAD_CREATE_DETACHED) { return EINVAL; }
  attr->detachstate = state;
  return 0;
}

_gnu_inline _gnu_nonnull(1)
int _sppc_pthread_attr_setstacksize(_sppc_pthread_attr_t *attr, const size_t size) {
  if (size < PTHREAD_STACK_MIN || size > UINT_MAX) { return EINVAL; }
  attr->stacksize = size;
  return 0;
}

_gnu_inline _gnu_nonnull(1, 3)
int _sppc_pthread_create(_sppc_pthread_t *out, _sppc_pthread_attr_t const *attr, void *(*start)(void*), void *arg) {
  const bool detached = attr != NULL && attr->detachstate == PTHREAD_CREATE_DETACHED;
  const unsigned stack = attr != NULL ? (unsigned)attr->stacksize : 0;
  _sppc_win_thread *const t = (_sppc_win_thread*)calloc(1, sizeof *t);
  if (t == NULL) { return EAGAIN; }
  t->start = start;
  t->arg = arg;
  t->refs = detached ? 1 : 2;
  t->joinable = !detached;

  // Suspended until the handle is stored: a detached thread that ran to
  // its end first would free "t" under this write.
  const uintptr_t handle = _beginthreadex(NULL, stack, _sppc_win_thread_start, t,
    CREATE_SUSPENDED | (stack != 0 ? STACK_SIZE_PARAM_IS_A_RESERVATION : 0), NULL);
  if (handle == 0) {
    free(t);
    return EAGAIN;
  }
  t->handle = (HANDLE)handle;
  if (ResumeThread(t->handle) == (DWORD)-1) {
    // It never ran a line of its own, so there is nothing to unwind.
    TerminateThread(t->handle, 0);
    CloseHandle(t->handle);
    free(t);
    return EAGAIN;
  }
  *out = t;
  return 0;
}

_gnu_inline _gnu_nonnull(1)
int _sppc_pthread_join(_sppc_pthread_t thread, void **retval) {
  if (thread == _sppc_win_self) { return EDEADLK; }
  if (!InterlockedExchange(&thread->joinable, 0)) { return EINVAL; }
  WaitForSingleObject(thread->handle, INFINITE);
  if (retval != NULL) { *retval = thread->retval; }
  _sppc_win_release(thread);
  return 0;
}

_gnu_inline _gnu_nonnull(1)
int _sppc_pthread_detach(_sppc_pthread_t thread) {
  if (!InterlockedExchange(&thread->joinable, 0)) { return EINVAL; }
  _sppc_win_release(thread);
  return 0;
}

_gnu_inline
int _sppc_pthread_equal(const _sppc_pthread_t a, const _sppc_pthread_t b) {
  return a == b;
}

/// POSIX gives this no way to fail, so a thread that cannot be adopted
/// (out of memory for its first use) ends the process.
_gnu_inline
_sppc_pthread_t _sppc_pthread_self(void) {
  const _sppc_pthread_t t = _sppc_win_current();
  if (t == NULL) { abort(); }
  return t;
}

_gnu_inline _gnu_nonnull(1)
int _sppc_pthread_key_create(_sppc_pthread_key_t *key, void (*destructor)(void*)) {
  int err = EAGAIN;
  AcquireSRWLockExclusive(&_sppc_win_keys_lock);
  for (DWORD k = 0; k < PTHREAD_KEYS_MAX; ++k) {
    if (_sppc_win_keys[k].seq & 1) { continue; }
    _sppc_win_keys[k].destructor = destructor;
    InterlockedIncrement(&_sppc_win_keys[k].seq);
    *key = k;
    err = 0;
    break;
  }
  ReleaseSRWLockExclusive(&_sppc_win_keys_lock);
  return err;
}

_gnu_inline
int _sppc_pthread_key_delete(const _sppc_pthread_key_t key) {
  if (key >= PTHREAD_KEYS_MAX) { return EINVAL; }
  int err = EINVAL;
  AcquireSRWLockExclusive(&_sppc_win_keys_lock);
  if (_sppc_win_keys[key].seq & 1) {
    InterlockedIncrement(&_sppc_win_keys[key].seq);
    _sppc_win_keys[key].destructor = NULL;
    err = 0;
  }
  ReleaseSRWLockExclusive(&_sppc_win_keys_lock);
  return err;
}

_gnu_inline
void* _sppc_pthread_getspecific(const _sppc_pthread_key_t key) {
  _sppc_win_thread const *const t = _sppc_win_self;
  if (t == NULL || key >= PTHREAD_KEYS_MAX) { return NULL; }
  _sppc_win_slot const *const slot = &t->slots[key];
  return slot->seq == _sppc_win_keys[key].seq ? slot->value : NULL;
}

_gnu_inline
int _sppc_pthread_setspecific(const _sppc_pthread_key_t key, void const *value) {
  if (key >= PTHREAD_KEYS_MAX) { return EINVAL; }
  const LONG seq = _sppc_win_keys[key].seq;
  if (!(seq & 1)) { return EINVAL; }
  _sppc_win_thread *const t = _sppc_win_current();
  if (t == NULL) { return ENOMEM; }
  t->slots[key].seq = seq;
  t->slots[key].value = (void*)value;
  return 0;
}

// ==================== PTHREADS: ONCE ====================

/// Windows' own one-time initialisation: a pointer, so it fits in an sppc
/// handle as "sppc_pthread_once" needs, and starts as zero.
typedef INIT_ONCE _sppc_once_t;
#define _SPPC_ONCE_INIT INIT_ONCE_STATIC_INIT

typedef struct _sppc_win_once_arg {
  void (*fn)(void);
} _sppc_win_once_arg;

_gnu_inline_va
BOOL CALLBACK _sppc_win_once_call(PINIT_ONCE once, PVOID param, PVOID *context) {
  (void)once;
  (void)context;
  ((_sppc_win_once_arg*)param)->fn();
  return TRUE;
}

_gnu_inline _gnu_nonnull(1, 2)
int _sppc_pthread_once(_sppc_once_t *once, void (*fn)(void)) {
  _sppc_win_once_arg arg = {fn};
  return InitOnceExecuteOnce(once, _sppc_win_once_call, &arg, NULL) ? 0 : EINVAL;
}

// ==================== PTHREADS: MUTEXES ====================

#define PTHREAD_MUTEX_NORMAL 0
#define PTHREAD_MUTEX_ERRORCHECK 1
#define PTHREAD_MUTEX_RECURSIVE 2
#define PTHREAD_MUTEX_DEFAULT PTHREAD_MUTEX_NORMAL

typedef struct _sppc_pthread_mutexattr_t {
  int kind;
} _sppc_pthread_mutexattr_t;

/// A futex-style mutex on "WaitOnAddress". "state" is 0 when free, 1 when
/// held, and 2 when held with a thread perhaps asleep on it, which the
/// unlock then wakes. "owner" is the holder's thread id, which only the
/// holder writes, so a thread reading its own id there really holds it.
typedef struct _sppc_pthread_mutex_t {
  volatile LONG state;
  volatile DWORD owner;
  unsigned count;
  int kind;
} _sppc_pthread_mutex_t;

#define PTHREAD_MUTEX_INITIALIZER {0, 0, 0, PTHREAD_MUTEX_DEFAULT}

_gnu_inline _gnu_nonnull(1)
int _sppc_pthread_mutexattr_init(_sppc_pthread_mutexattr_t *attr) {
  attr->kind = PTHREAD_MUTEX_DEFAULT;
  return 0;
}

_gnu_inline _gnu_nonnull(1)
int _sppc_pthread_mutexattr_destroy(_sppc_pthread_mutexattr_t *attr) {
  (void)attr;
  return 0;
}

_gnu_inline _gnu_nonnull(1)
int _sppc_pthread_mutexattr_settype(_sppc_pthread_mutexattr_t *attr, const int kind) {
  if (kind != PTHREAD_MUTEX_NORMAL && kind != PTHREAD_MUTEX_ERRORCHECK && kind != PTHREAD_MUTEX_RECURSIVE) {
    return EINVAL;
  }
  attr->kind = kind;
  return 0;
}

_gnu_inline _gnu_nonnull(1, 2)
int _sppc_pthread_mutexattr_gettype(_sppc_pthread_mutexattr_t const *attr, int *kind) {
  *kind = attr->kind;
  return 0;
}

_gnu_inline _gnu_nonnull(1)
int _sppc_pthread_mutex_init(_sppc_pthread_mutex_t *mutex, _sppc_pthread_mutexattr_t const *attr) {
  mutex->state = 0;
  mutex->owner = 0;
  mutex->count = 0;
  mutex->kind = attr != NULL ? attr->kind : PTHREAD_MUTEX_DEFAULT;
  return 0;
}

_gnu_inline _gnu_nonnull(1)
int _sppc_pthread_mutex_destroy(_sppc_pthread_mutex_t *mutex) {
  return mutex->state != 0 ? EBUSY : 0;
}

/// Take a recursive mutex again, or refuse an error-checking one, if this
/// thread already holds it. -1 when it does not, and the lock goes ahead.
/// A normal mutex never says so: taking it again deadlocks, as POSIX has it.
_gnu_inline _gnu_nonnull(1)
int _sppc_win_mutex_relock(_sppc_pthread_mutex_t *mutex) {
  if (mutex->kind == PTHREAD_MUTEX_NORMAL || mutex->owner != GetCurrentThreadId()) { return -1; }
  if (mutex->kind == PTHREAD_MUTEX_ERRORCHECK) { return EDEADLK; }
  if (mutex->count == UINT_MAX) { return EAGAIN; }
  ++mutex->count;
  return 0;
}

_gnu_inline _gnu_nonnull(1)
void _sppc_win_mutex_acquired(_sppc_pthread_mutex_t *mutex) {
  mutex->owner = GetCurrentThreadId();
  mutex->count = 1;
}

/// The lock behind lock, clocklock and timedlock: NULL @p deadline waits for
/// as long as it takes. A thread that has to sleep marks the mutex
/// contended first, so whoever holds it wakes one sleeper on release.
_gnu_inline _gnu_nonnull(1)
int _sppc_win_mutex_lock(_sppc_pthread_mutex_t *mutex, const clockid_t clock, struct timespec const *deadline) {
  const int relocked = _sppc_win_mutex_relock(mutex);
  if (relocked >= 0) { return relocked; }
  if (InterlockedCompareExchange(&mutex->state, 1, 0) != 0) {
    while (InterlockedExchange(&mutex->state, 2) != 0) {
      LONG contended = 2;
      const int err = _sppc_win_wait(&mutex->state, &contended, sizeof contended, clock, deadline);
      if (err != 0) { return err; }
    }
  }
  _sppc_win_mutex_acquired(mutex);
  return 0;
}

_gnu_inline _gnu_nonnull(1)
int _sppc_pthread_mutex_lock(_sppc_pthread_mutex_t *mutex) {
  return _sppc_win_mutex_lock(mutex, CLOCK_MONOTONIC, NULL);
}

/// EBUSY for a held mutex, including one this thread holds, unless it is
/// recursive; as glibc's.
_gnu_inline _gnu_nonnull(1)
int _sppc_pthread_mutex_trylock(_sppc_pthread_mutex_t *mutex) {
  if (mutex->kind == PTHREAD_MUTEX_RECURSIVE && mutex->owner == GetCurrentThreadId()) {
    return _sppc_win_mutex_relock(mutex);
  }
  if (InterlockedCompareExchange(&mutex->state, 1, 0) != 0) { return EBUSY; }
  _sppc_win_mutex_acquired(mutex);
  return 0;
}

_gnu_inline _gnu_nonnull(1, 3)
int _sppc_pthread_mutex_clocklock(_sppc_pthread_mutex_t *mutex, const clockid_t clock,
  struct timespec const *deadline) {
  const int err = _sppc_win_check_deadline(clock, deadline);
  return err != 0 ? err : _sppc_win_mutex_lock(mutex, clock, deadline);
}

_gnu_inline _gnu_nonnull(1, 2)
int _sppc_pthread_mutex_timedlock(_sppc_pthread_mutex_t *mutex, struct timespec const *deadline) {
  return _sppc_pthread_mutex_clocklock(mutex, CLOCK_REALTIME, deadline);
}

/// EPERM from a thread that does not hold an error-checking or recursive
/// mutex; a normal one is not checked, as glibc does not.
_gnu_inline _gnu_nonnull(1)
int _sppc_pthread_mutex_unlock(_sppc_pthread_mutex_t *mutex) {
  if (mutex->kind != PTHREAD_MUTEX_NORMAL) {
    if (mutex->owner != GetCurrentThreadId()) { return EPERM; }
    if (mutex->kind == PTHREAD_MUTEX_RECURSIVE && --mutex->count != 0) { return 0; }
  }
  mutex->owner = 0;
  if (InterlockedExchange(&mutex->state, 0) == 2) { WakeByAddressSingle((PVOID)&mutex->state); }
  return 0;
}

// ==================== PTHREADS: CONDITION VARIABLES ====================

typedef struct _sppc_pthread_condattr_t {
  clockid_t clock;
} _sppc_pthread_condattr_t;

/// "seq" moves on with every signal and broadcast. A waiter notes it while
/// still holding the mutex and sleeps only while it is unchanged, so a
/// signal between its unlock and its sleep is never missed. "clock" is what
/// "timedwait" counts in (real time unless the attributes say otherwise).
typedef struct _sppc_pthread_cond_t {
  volatile LONG seq;
  clockid_t clock;
} _sppc_pthread_cond_t;

#define PTHREAD_COND_INITIALIZER {0, CLOCK_REALTIME}

_gnu_inline _gnu_nonnull(1)
int _sppc_pthread_condattr_init(_sppc_pthread_condattr_t *attr) {
  attr->clock = CLOCK_REALTIME;
  return 0;
}

_gnu_inline _gnu_nonnull(1)
int _sppc_pthread_condattr_destroy(_sppc_pthread_condattr_t *attr) {
  (void)attr;
  return 0;
}

_gnu_inline _gnu_nonnull(1)
int _sppc_pthread_condattr_setclock(_sppc_pthread_condattr_t *attr, const clockid_t clock) {
  if (clock != CLOCK_REALTIME && clock != CLOCK_MONOTONIC) { return EINVAL; }
  attr->clock = clock;
  return 0;
}

_gnu_inline _gnu_nonnull(1, 2)
int _sppc_pthread_condattr_getclock(_sppc_pthread_condattr_t const *attr, clockid_t *clock) {
  *clock = attr->clock;
  return 0;
}

_gnu_inline _gnu_nonnull(1)
int _sppc_pthread_cond_init(_sppc_pthread_cond_t *cond, _sppc_pthread_condattr_t const *attr) {
  cond->seq = 0;
  cond->clock = attr != NULL ? attr->clock : CLOCK_REALTIME;
  return 0;
}

_gnu_inline _gnu_nonnull(1)
int _sppc_pthread_cond_destroy(_sppc_pthread_cond_t *cond) {
  (void)cond;
  return 0;
}

_gnu_inline _gnu_nonnull(1)
int _sppc_pthread_cond_signal(_sppc_pthread_cond_t *cond) {
  InterlockedIncrement(&cond->seq);
  WakeByAddressSingle((PVOID)&cond->seq);
  return 0;
}

_gnu_inline _gnu_nonnull(1)
int _sppc_pthread_cond_broadcast(_sppc_pthread_cond_t *cond) {
  InterlockedIncrement(&cond->seq);
  WakeByAddressAll((PVOID)&cond->seq);
  return 0;
}

/// The wait behind wait, clockwait and timedwait: NULL @p deadline waits
/// until signalled. Like any condition wait it may also return spuriously,
/// which callers re-check their predicate for. The mutex is held again on
/// the way out whatever happened, and an error taking it back wins.
_gnu_inline _gnu_nonnull(1, 2)
int _sppc_win_cond_wait(_sppc_pthread_cond_t *cond, _sppc_pthread_mutex_t *mutex, const clockid_t clock,
  struct timespec const *deadline) {
  const int bad = _sppc_win_check_deadline(clock, deadline);
  if (bad != 0) { return bad; }
  LONG seen = cond->seq;
  const int uerr = _sppc_pthread_mutex_unlock(mutex);
  if (uerr != 0) { return uerr; }
  int err = 0;
  while (cond->seq == seen) {
    err = _sppc_win_wait(&cond->seq, &seen, sizeof seen, clock, deadline);
    // Without a deadline any wake ends the wait (a spurious one is allowed);
    // with one, a sleep may only have run out its milliseconds.
    if (err != 0 || deadline == NULL) { break; }
  }
  // Retaken without a deadline: the caller is owed the mutex back.
  const int lerr = _sppc_win_mutex_lock(mutex, clock, NULL);
  return lerr != 0 ? lerr : err;
}

_gnu_inline _gnu_nonnull(1, 2)
int _sppc_pthread_cond_wait(_sppc_pthread_cond_t *cond, _sppc_pthread_mutex_t *mutex) {
  return _sppc_win_cond_wait(cond, mutex, cond->clock, NULL);
}

_gnu_inline _gnu_nonnull(1, 2, 4)
int _sppc_pthread_cond_clockwait(_sppc_pthread_cond_t *cond, _sppc_pthread_mutex_t *mutex, const clockid_t clock,
  struct timespec const *deadline) {
  return _sppc_win_cond_wait(cond, mutex, clock, deadline);
}

_gnu_inline _gnu_nonnull(1, 2, 3)
int _sppc_pthread_cond_timedwait(_sppc_pthread_cond_t *cond, _sppc_pthread_mutex_t *mutex,
  struct timespec const *deadline) {
  return _sppc_win_cond_wait(cond, mutex, cond->clock, deadline);
}

// ==================== PTHREADS: RWLOCKS ====================

typedef struct _sppc_pthread_rwlockattr_t {
  int unused;
} _sppc_pthread_rwlockattr_t;

/// "state" is 0 when free, -1 when held for writing, and otherwise the
/// number of readers holding it. Readers come first, as glibc's default:
/// one is let in whenever no writer holds the lock, so a thread may take a
/// read lock it already has while a writer waits. Every unlock that frees
/// the lock wakes all its sleepers to race for it. "writer" is the writing
/// thread's id, for refusing that thread the lock again.
typedef struct _sppc_pthread_rwlock_t {
  volatile LONG state;
  volatile DWORD writer;
} _sppc_pthread_rwlock_t;

#define PTHREAD_RWLOCK_INITIALIZER {0, 0}

_gnu_inline _gnu_nonnull(1)
int _sppc_pthread_rwlock_init(_sppc_pthread_rwlock_t *rwlock, _sppc_pthread_rwlockattr_t const *attr) {
  (void)attr;
  rwlock->state = 0;
  rwlock->writer = 0;
  return 0;
}

_gnu_inline _gnu_nonnull(1)
int _sppc_pthread_rwlock_destroy(_sppc_pthread_rwlock_t *rwlock) {
  return rwlock->state != 0 ? EBUSY : 0;
}

_gnu_inline _gnu_nonnull(1)
int _sppc_pthread_rwlock_tryrdlock(_sppc_pthread_rwlock_t *rwlock) {
  for (;;) {
    const LONG state = rwlock->state;
    if (state < 0) { return EBUSY; }
    if (state == LONG_MAX) { return EAGAIN; }
    if (InterlockedCompareExchange(&rwlock->state, state + 1, state) == state) { return 0; }
  }
}

_gnu_inline _gnu_nonnull(1)
int _sppc_pthread_rwlock_trywrlock(_sppc_pthread_rwlock_t *rwlock) {
  if (InterlockedCompareExchange(&rwlock->state, -1, 0) != 0) { return EBUSY; }
  rwlock->writer = GetCurrentThreadId();
  return 0;
}

/// The lock behind every blocking read lock: NULL @p deadline waits for as
/// long as it takes. EDEADLK for the thread already writing, as glibc.
_gnu_inline _gnu_nonnull(1)
int _sppc_win_rwlock_rdlock(_sppc_pthread_rwlock_t *rwlock, const clockid_t clock, struct timespec const *deadline) {
  for (;;) {
    const int err = _sppc_pthread_rwlock_tryrdlock(rwlock);
    if (err != EBUSY) { return err; }
    LONG state = -1;
    if (rwlock->writer == GetCurrentThreadId()) { return EDEADLK; }
    const int werr = _sppc_win_wait(&rwlock->state, &state, sizeof state, clock, deadline);
    if (werr != 0) { return werr; }
  }
}

_gnu_inline _gnu_nonnull(1)
int _sppc_win_rwlock_wrlock(_sppc_pthread_rwlock_t *rwlock, const clockid_t clock, struct timespec const *deadline) {
  for (;;) {
    LONG state = rwlock->state;
    if (state == 0) {
      if (_sppc_pthread_rwlock_trywrlock(rwlock) == 0) { return 0; }
      continue;
    }
    if (state < 0 && rwlock->writer == GetCurrentThreadId()) { return EDEADLK; }
    const int err = _sppc_win_wait(&rwlock->state, &state, sizeof state, clock, deadline);
    if (err != 0) { return err; }
  }
}

_gnu_inline _gnu_nonnull(1)
int _sppc_pthread_rwlock_rdlock(_sppc_pthread_rwlock_t *rwlock) {
  return _sppc_win_rwlock_rdlock(rwlock, CLOCK_MONOTONIC, NULL);
}

_gnu_inline _gnu_nonnull(1)
int _sppc_pthread_rwlock_wrlock(_sppc_pthread_rwlock_t *rwlock) {
  return _sppc_win_rwlock_wrlock(rwlock, CLOCK_MONOTONIC, NULL);
}

_gnu_inline _gnu_nonnull(1, 3)
int _sppc_pthread_rwlock_clockrdlock(_sppc_pthread_rwlock_t *rwlock, const clockid_t clock,
  struct timespec const *deadline) {
  const int err = _sppc_win_check_deadline(clock, deadline);
  return err != 0 ? err : _sppc_win_rwlock_rdlock(rwlock, clock, deadline);
}

_gnu_inline _gnu_nonnull(1, 3)
int _sppc_pthread_rwlock_clockwrlock(_sppc_pthread_rwlock_t *rwlock, const clockid_t clock,
  struct timespec const *deadline) {
  const int err = _sppc_win_check_deadline(clock, deadline);
  return err != 0 ? err : _sppc_win_rwlock_wrlock(rwlock, clock, deadline);
}

_gnu_inline _gnu_nonnull(1, 2)
int _sppc_pthread_rwlock_timedrdlock(_sppc_pthread_rwlock_t *rwlock, struct timespec const *deadline) {
  return _sppc_pthread_rwlock_clockrdlock(rwlock, CLOCK_REALTIME, deadline);
}

_gnu_inline _gnu_nonnull(1, 2)
int _sppc_pthread_rwlock_timedwrlock(_sppc_pthread_rwlock_t *rwlock, struct timespec const *deadline) {
  return _sppc_pthread_rwlock_clockwrlock(rwlock, CLOCK_REALTIME, deadline);
}

/// EPERM for a lock nobody holds, or a write lock this thread is not the
/// writer of; a read lock cannot say which threads hold it.
_gnu_inline _gnu_nonnull(1)
int _sppc_pthread_rwlock_unlock(_sppc_pthread_rwlock_t *rwlock) {
  const LONG state = rwlock->state;
  if (state == 0) { return EPERM; }
  if (state < 0) {
    if (rwlock->writer != GetCurrentThreadId()) { return EPERM; }
    rwlock->writer = 0;
    InterlockedExchange(&rwlock->state, 0);
    WakeByAddressAll((PVOID)&rwlock->state);
  } else if (InterlockedDecrement(&rwlock->state) == 0) {
    WakeByAddressAll((PVOID)&rwlock->state);
  }
  return 0;
}

// ==================== PTHREADS: SPIN LOCKS ====================

/// A LONG, so it fits in an sppc handle as "sppc_pthread_spin_init" needs.
typedef LONG _sppc_spinlock_t;

_gnu_inline _gnu_nonnull(1)
int _sppc_pthread_spin_init(_sppc_spinlock_t *spinlock, const int pshared) {
  (void)pshared;
  InterlockedExchange(spinlock, 0);
  return 0;
}

_gnu_inline _gnu_nonnull(1)
int _sppc_pthread_spin_destroy(_sppc_spinlock_t *spinlock) {
  (void)spinlock;
  return 0;
}

_gnu_inline _gnu_nonnull(1)
int _sppc_pthread_spin_trylock(_sppc_spinlock_t *spinlock) {
  return InterlockedExchange(spinlock, 1) == 0 ? 0 : EBUSY;
}

/// Spins on a plain read, so a waiting core reads its own cached copy of
/// the line until the release, and now and then gives the cpu up in case
/// the holder was descheduled.
_gnu_inline _gnu_nonnull(1)
int _sppc_pthread_spin_lock(_sppc_spinlock_t *spinlock) {
  while (InterlockedExchange(spinlock, 1) != 0) {
    for (unsigned spins = 1; *(volatile LONG*)spinlock != 0; ++spins) {
      YieldProcessor();
      if (spins % 1024 == 0) { SwitchToThread(); }
    }
  }
  return 0;
}

_gnu_inline _gnu_nonnull(1)
int _sppc_pthread_spin_unlock(_sppc_spinlock_t *spinlock) {
  InterlockedExchange(spinlock, 0);
  return 0;
}

// ==================== PTHREADS: BARRIERS ====================

#define PTHREAD_BARRIER_SERIAL_THREAD (-1)

typedef struct _sppc_pthread_barrierattr_t {
  int unused;
} _sppc_pthread_barrierattr_t;

/// "generation" moves on each time the barrier opens, which is what its
/// waiters sleep on. "waiters" counts threads still inside a wait, so a
/// destroy (straight after the last thread arrives, say) waits for them to
/// stop reading it before the memory can go.
typedef struct _sppc_pthread_barrier_t {
  SRWLOCK lock;
  LONG count;
  LONG remaining;
  volatile LONG generation;
  volatile LONG waiters;
} _sppc_pthread_barrier_t;

_gnu_inline _gnu_nonnull(1)
int _sppc_pthread_barrier_init(_sppc_pthread_barrier_t *barrier, _sppc_pthread_barrierattr_t const *attr,
  const unsigned count) {
  (void)attr;
  if (count == 0 || count > LONG_MAX) { return EINVAL; }
  InitializeSRWLock(&barrier->lock);
  barrier->count = barrier->remaining = (LONG)count;
  barrier->generation = 0;
  barrier->waiters = 0;
  return 0;
}

/// EBUSY while a round is part-way: some threads have arrived and are
/// waiting for the rest.
_gnu_inline _gnu_nonnull(1)
int _sppc_pthread_barrier_destroy(_sppc_pthread_barrier_t *barrier) {
  AcquireSRWLockExclusive(&barrier->lock);
  const bool busy = barrier->remaining != barrier->count;
  ReleaseSRWLockExclusive(&barrier->lock);
  if (busy) { return EBUSY; }
  for (;;) {
    LONG waiters = barrier->waiters;
    if (waiters == 0) { break; }
    WaitOnAddress(&barrier->waiters, &waiters, sizeof waiters, INFINITE);
  }
  return 0;
}

_gnu_inline _gnu_nonnull(1)
int _sppc_pthread_barrier_wait(_sppc_pthread_barrier_t *barrier) {
  AcquireSRWLockExclusive(&barrier->lock);
  LONG generation = barrier->generation;
  if (--barrier->remaining == 0) {
    barrier->remaining = barrier->count;
    InterlockedIncrement(&barrier->generation);
    ReleaseSRWLockExclusive(&barrier->lock);
    WakeByAddressAll((PVOID)&barrier->generation);
    return PTHREAD_BARRIER_SERIAL_THREAD;
  }
  InterlockedIncrement(&barrier->waiters);
  ReleaseSRWLockExclusive(&barrier->lock);
  while (barrier->generation == generation) {
    WaitOnAddress(&barrier->generation, &generation, sizeof generation, INFINITE);
  }
  // The last one out may let a destroy free the barrier; a wake needs only
  // the address, not the memory behind it.
  if (InterlockedDecrement(&barrier->waiters) == 0) { WakeByAddressAll((PVOID)&barrier->waiters); }
  return 0;
}

// ==================== PTHREADS: POSIX NAMES ====================

#define pthread_t _sppc_pthread_t
#define pthread_attr_t _sppc_pthread_attr_t
#define pthread_key_t _sppc_pthread_key_t
#define pthread_once_t _sppc_once_t
#define pthread_mutex_t _sppc_pthread_mutex_t
#define pthread_mutexattr_t _sppc_pthread_mutexattr_t
#define pthread_cond_t _sppc_pthread_cond_t
#define pthread_condattr_t _sppc_pthread_condattr_t
#define pthread_rwlock_t _sppc_pthread_rwlock_t
#define pthread_rwlockattr_t _sppc_pthread_rwlockattr_t
#define pthread_spinlock_t _sppc_spinlock_t
#define pthread_barrier_t _sppc_pthread_barrier_t
#define pthread_barrierattr_t _sppc_pthread_barrierattr_t
#define PTHREAD_ONCE_INIT _SPPC_ONCE_INIT

#define pthread_create _sppc_pthread_create
#define pthread_join _sppc_pthread_join
#define pthread_detach _sppc_pthread_detach
#define pthread_equal _sppc_pthread_equal
#define pthread_self _sppc_pthread_self
#define pthread_attr_init _sppc_pthread_attr_init
#define pthread_attr_destroy _sppc_pthread_attr_destroy
#define pthread_attr_setdetachstate _sppc_pthread_attr_setdetachstate
#define pthread_attr_setstacksize _sppc_pthread_attr_setstacksize
#define pthread_key_create _sppc_pthread_key_create
#define pthread_key_delete _sppc_pthread_key_delete
#define pthread_getspecific _sppc_pthread_getspecific
#define pthread_setspecific _sppc_pthread_setspecific
#define pthread_once _sppc_pthread_once
#define pthread_mutexattr_init _sppc_pthread_mutexattr_init
#define pthread_mutexattr_destroy _sppc_pthread_mutexattr_destroy
#define pthread_mutexattr_settype _sppc_pthread_mutexattr_settype
#define pthread_mutexattr_gettype _sppc_pthread_mutexattr_gettype
#define pthread_mutex_init _sppc_pthread_mutex_init
#define pthread_mutex_destroy _sppc_pthread_mutex_destroy
#define pthread_mutex_lock _sppc_pthread_mutex_lock
#define pthread_mutex_trylock _sppc_pthread_mutex_trylock
#define pthread_mutex_timedlock _sppc_pthread_mutex_timedlock
#define pthread_mutex_unlock _sppc_pthread_mutex_unlock
#define pthread_condattr_init _sppc_pthread_condattr_init
#define pthread_condattr_destroy _sppc_pthread_condattr_destroy
#define pthread_condattr_setclock _sppc_pthread_condattr_setclock
#define pthread_condattr_getclock _sppc_pthread_condattr_getclock
#define pthread_cond_init _sppc_pthread_cond_init
#define pthread_cond_destroy _sppc_pthread_cond_destroy
#define pthread_cond_signal _sppc_pthread_cond_signal
#define pthread_cond_broadcast _sppc_pthread_cond_broadcast
#define pthread_cond_wait _sppc_pthread_cond_wait
#define pthread_cond_timedwait _sppc_pthread_cond_timedwait
#define pthread_rwlock_init _sppc_pthread_rwlock_init
#define pthread_rwlock_destroy _sppc_pthread_rwlock_destroy
#define pthread_rwlock_rdlock _sppc_pthread_rwlock_rdlock
#define pthread_rwlock_wrlock _sppc_pthread_rwlock_wrlock
#define pthread_rwlock_tryrdlock _sppc_pthread_rwlock_tryrdlock
#define pthread_rwlock_trywrlock _sppc_pthread_rwlock_trywrlock
#define pthread_rwlock_timedrdlock _sppc_pthread_rwlock_timedrdlock
#define pthread_rwlock_timedwrlock _sppc_pthread_rwlock_timedwrlock
#define pthread_rwlock_unlock _sppc_pthread_rwlock_unlock
#define pthread_spin_init _sppc_pthread_spin_init
#define pthread_spin_destroy _sppc_pthread_spin_destroy
#define pthread_spin_lock _sppc_pthread_spin_lock
#define pthread_spin_trylock _sppc_pthread_spin_trylock
#define pthread_spin_unlock _sppc_pthread_spin_unlock
#define pthread_barrier_init _sppc_pthread_barrier_init
#define pthread_barrier_destroy _sppc_pthread_barrier_destroy
#define pthread_barrier_wait _sppc_pthread_barrier_wait

// ==================== RUNTIME ====================

// The async runtime and the crash reporter, over the pthreads and clocks
// above; the api below takes its blocking calls and locks through them.
#include <sppc/async.h>
#include <sppc/backtrace.h>

// ==================== TRANSLATION ====================

// The machinery, and the tables every platform shares (sockets, poll), are
// platform/shared.h's; what follows is Windows' own.
#include <sppc/platform/shared.h>

// As platform/posix.h's: sppc's values to the platform's and back, the
// platform here being the clocks built above (PTHREADS: CLOCKS).
// Windows has the four POSIX clocks; Linux's own (raw, coarse, boot time)
// are refused, as on any platform without them.

_gnu_inline _gnu_nonnull(2)
int _sppc_to_os_clock(const int clock, clockid_t *_sppc_restrict out) {
  switch (clock) {
    case SPPC_CLOCK_REALTIME: *out = CLOCK_REALTIME; return 0;
    case SPPC_CLOCK_MONOTONIC: *out = CLOCK_MONOTONIC; return 0;
    case SPPC_CLOCK_PROCESS_CPUTIME_ID: *out = CLOCK_PROCESS_CPUTIME_ID; return 0;
    case SPPC_CLOCK_THREAD_CPUTIME_ID: *out = CLOCK_THREAD_CPUTIME_ID; return 0;
    default: return SPPC_EINVAL;
  }
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

_gnu_inline _gnu_nonnull(2)
int _sppc_to_os_timer_flags(const int flags, int *_sppc_restrict out) {
  if ((flags & ~SPPC_TIMER_ABSTIME) != 0) { return SPPC_EINVAL; }
  *out = (flags & SPPC_TIMER_ABSTIME) ? TIMER_ABSTIME : 0;
  return 0;
}

// ==================== BOOT CODE ====================

// The standard streams' locks (sppc.c).
extern pthread_mutex_t _stdin_mutex;
extern pthread_mutex_t _stdout_mutex;
extern pthread_mutex_t _stderr_mutex;

// What the POSIX boot does with signals has nothing to do here: a write to
// a pipe with no reader is already an error (EPIPE) rather than a signal,
// no child signal needs its handler reset, and a closed console cannot be
// ignored. Cancellation and "mallopt" do not exist either. The crash
// reporter (backtrace/pe.h) is not written yet.
_gnu_inline _gnu_cold
_sppc_api int sppc_init(void) {
  // A drive with no medium in it (an empty card reader, say) fails the
  // call, as a POSIX one would, rather than stopping the process on a
  // dialog box asking for a disk.
  SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS);
  setlocale(LC_ALL, "en_GB.UTF-8"); // as on POSIX; mingw's msvcrt has no UTF-8 locales and leaves "C"
  _tzset();
  _gt_init(); // initialize the async green thread runtime.
  _sppc_bt_install(); // report an unhandled exception (a fault, a stack overflow) or an abort, with a backtrace

  // Winsock: every socket call fails until it has been started. Counted, so
  // a host that started it too is unaffected, and "sppc_cleanup" undoes
  // only this one.
  WSADATA wsa;
  const int wsa_err = WSAStartup(MAKEWORD(2, 2), &wsa);
  if (wsa_err != 0) { return _sppc_errno_wsa(wsa_err); }

  _return_if_pthread_err(pthread_mutex_init(&_stdin_mutex, NULL))
  _return_if_pthread_err(pthread_mutex_init(&_stdout_mutex, NULL))
  _return_if_pthread_err(pthread_mutex_init(&_stderr_mutex, NULL))
  return 0;
}

_gnu_inline _gnu_cold
_sppc_api int sppc_cleanup(void) {
  // As on POSIX: every task still runnable finishes, so one spawned and
  // never awaited still releases what it captured.
  _gt_drain();

  _return_if_pthread_err(pthread_mutex_destroy(&_stdin_mutex))
  _return_if_pthread_err(pthread_mutex_destroy(&_stdout_mutex))
  _return_if_pthread_err(pthread_mutex_destroy(&_stderr_mutex))
  if (WSACleanup() != 0) { return _sppc_errno_wsa(WSAGetLastError()); }
  return 0;
}

// ==================== SPLIT STACK ====================

// As in platform/posix.h. llvm has no SafeStack for Windows targets, so
// nothing compiled for Windows reads the pointer yet; the runtime keeps
// it per task all the same, as it does everywhere.
extern _sppc_thread_local void *__safestack_unsafe_stack_ptr;
extern _sppc_thread_local void *_unsafe_stack_base;
extern _sppc_thread_local size_t _unsafe_stack_size;
extern bool _unsafe_stack_wanted;

#define UNSAFE_STACK_MIN (1024 * 1024)
#define UNSAFE_STACK_MAX (64 * 1024 * 1024)

/// Sized off this thread's own stack (its reserve), clamped as on POSIX,
/// with a guard page at the low end. Committed whole: Windows backs no
/// memory lazily apart from the thread's real stack.
_gnu_inline _gnu_cold
_sppc_api int sppc_unsafe_stack_up(void) {
  if (_unsafe_stack_base != NULL) { return 0; }
  _unsafe_stack_wanted = true;

  ULONG_PTR low = 0, high = 0;
  GetCurrentThreadStackLimits(&low, &high);
  size_t size = (size_t)(high - low);
  if (size < UNSAFE_STACK_MIN) { size = UNSAFE_STACK_MIN; }
  if (size > UNSAFE_STACK_MAX) { size = UNSAFE_STACK_MAX; }

  SYSTEM_INFO info;
  GetSystemInfo(&info);
  const size_t page = (size_t)info.dwPageSize;
  size = (size + page - 1) & ~(page - 1);
  const size_t total = size + page;

  char *const p = (char*)VirtualAlloc(NULL, total, MEM_RESERVE, PAGE_NOACCESS);
  if (p == NULL) { return _sppc_errno_win32(GetLastError()); }
  if (VirtualAlloc(p + page, size, MEM_COMMIT, PAGE_READWRITE) == NULL) {
    const DWORD err = GetLastError();
    VirtualFree(p, 0, MEM_RELEASE);
    return _sppc_errno_win32(err);
  }

  _unsafe_stack_base = p;
  _unsafe_stack_size = total;
  __safestack_unsafe_stack_ptr = p + total;
  return 0;
}

_gnu_inline _gnu_cold
_sppc_api void sppc_unsafe_stack_down(void) {
  if (_unsafe_stack_base == NULL) { return; }
  VirtualFree(_unsafe_stack_base, 0, MEM_RELEASE);
  _unsafe_stack_base = NULL;
  _unsafe_stack_size = 0;
  __safestack_unsafe_stack_ptr = NULL;
}

// ==================== HEAP ALLOCATION ====================

// msvc's C library has no "aligned_alloc", and what it has instead
// ("_aligned_malloc") must be freed with "_aligned_free" and resized with
// the alignment it was made with, so a "sppc_free" or "sppc_realloc" that
// cannot tell how a block was made has no call to give it to. Every block
// sppc hands out therefore starts with a header of its own, 16 bytes below
// the pointer: the C library's block it sits in, and the size asked for.
// A plain block sits 16 bytes into its C library block (whose own 16-byte
// alignment it keeps); an over-aligned one wherever its alignment puts it.
//
// So whatever sppc frees or resizes must have come from these, and the
// runtime frees each task's environment: on Windows the caller allocates it
// with "sppc_malloc" (as anything crossing the dll boundary must, each
// module's C library possibly having a heap of its own).

typedef struct _sppc_heap_head {
  void *raw;
  size_t size;
} _sppc_heap_head;

#define _SPPC_HEAP_HEAD 16u
_Static_assert(sizeof(_sppc_heap_head) <= _SPPC_HEAP_HEAD, "the heap header must fit below the block");
_Static_assert(MEMORY_ALLOCATION_ALIGNMENT == _SPPC_HEAP_HEAD, "malloc's alignment is what a plain block keeps");

_gnu_inline _gnu_nonnull(1)
_sppc_heap_head* _sppc_heap_head_of(void *ptr) {
  return (_sppc_heap_head*)((char*)ptr - _SPPC_HEAP_HEAD);
}

/// The block "raw" (NULL when the C library had none) holds "size" bytes
/// aligned to "align" (a power of two, at least the header's size), with
/// the header just below them.
_gnu_inline
void* _sppc_heap_place(void *raw, const size_t size, const size_t align) {
  if (raw == NULL) { return NULL; }
  const uintptr_t user = ((uintptr_t)raw + _SPPC_HEAP_HEAD + (align - 1)) & ~(uintptr_t)(align - 1);
  _sppc_heap_head *const head = _sppc_heap_head_of((void*)user);
  head->raw = raw;
  head->size = size;
  return (void*)user;
}

/// A size too big to carry its header is out of memory, as glibc's
/// "malloc" says of a size it cannot have.
_gnu_inline
bool _sppc_heap_overflows(const size_t size, const size_t extra) {
  if (size <= SIZE_MAX - extra) { return false; }
  errno = ENOMEM;
  return true;
}

_gnu_inline _gnu_hot _gnu_malloc _gnu_alloc_size(1)
_sppc_api void* sppc_malloc(const size_t size) {
  if (_sppc_heap_overflows(size, _SPPC_HEAP_HEAD)) { return NULL; }
  _extract_err(void*) _sppc_heap_place(malloc(size + _SPPC_HEAP_HEAD), size, _SPPC_HEAP_HEAD);
  _return_pointer
}

/// As glibc's, any power of two is an alignment, and anything else is
/// refused with EINVAL. Up to malloc's own 16 bytes, a plain block.
_gnu_inline _gnu_malloc _gnu_alloc_size(1) _gnu_alloc_align(2)
_sppc_api void* sppc_aligned_alloc(const size_t size, const size_t alignment) {
  if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
    errno = EINVAL;
    return NULL;
  }
  if (alignment <= _SPPC_HEAP_HEAD) { return sppc_malloc(size); }
  if (_sppc_heap_overflows(size, _SPPC_HEAP_HEAD + alignment)) { return NULL; }
  _extract_err(void*) _sppc_heap_place(malloc(size + _SPPC_HEAP_HEAD + alignment), size, alignment);
  _return_pointer
}

_gnu_inline _gnu_hot _gnu_malloc _gnu_alloc_size(1, 2)
_sppc_api void* sppc_calloc(const size_t num, const size_t size) {
  if (size != 0 && num > SIZE_MAX / size) {
    errno = ENOMEM;
    return NULL;
  }
  const size_t total = num * size;
  if (_sppc_heap_overflows(total, _SPPC_HEAP_HEAD)) { return NULL; }
  _extract_err(void*) _sppc_heap_place(calloc(1, total + _SPPC_HEAP_HEAD), total, _SPPC_HEAP_HEAD);
  _return_pointer
}

_gnu_inline _gnu_hot
_sppc_api void sppc_free(void *ptr) {
  if (ptr != NULL) { free(_sppc_heap_head_of(ptr)->raw); }
}

/// As glibc's: a NULL block is a new one, a size of 0 frees the block and
/// gives NULL, and a failure leaves the block as it was. A plain block is
/// the C library's "realloc"; an over-aligned one moves to a plain one,
/// as POSIX keeps no alignment across a realloc either.
_gnu_inline _gnu_hot _gnu_alloc_size(2)
_sppc_api void* sppc_realloc(void *ptr, const size_t new_size) {
  if (ptr == NULL) { return sppc_malloc(new_size); }
  if (new_size == 0) {
    sppc_free(ptr);
    return NULL;
  }
  _sppc_heap_head *const head = _sppc_heap_head_of(ptr);
  if (head->raw == (void*)head) {
    if (_sppc_heap_overflows(new_size, _SPPC_HEAP_HEAD)) { return NULL; }
    _extract_err(void*) _sppc_heap_place(realloc(head->raw, new_size + _SPPC_HEAP_HEAD), new_size, _SPPC_HEAP_HEAD);
    _return_pointer
  }
  void *const moved = sppc_malloc(new_size);
  if (moved == NULL) { return NULL; }
  memcpy(moved, ptr, head->size < new_size ? head->size : new_size);
  free(head->raw);
  return moved;
}

// ==================== MEMORY ====================

/// glibc's: the first occurrence of the needle, the haystack itself for an
/// empty one, NULL for none. Each candidate is found by its first byte, as
/// "memchr" does that fastest, then compared whole.
_gnu_inline
void* _sppc_memmem(void const *haystack, const size_t haystack_size, void const *needle, const size_t needle_size) {
  if (needle_size == 0) { return (void*)haystack; }
  if (needle_size > haystack_size) { return NULL; }
  char const *h = (char const*)haystack;
  char const *const last = h + (haystack_size - needle_size);
  const int first = *(unsigned char const*)needle;
  while (h <= last) {
    char const *const hit = (char const*)memchr(h, first, (size_t)(last - h) + 1);
    if (hit == NULL) { return NULL; }
    if (memcmp(hit, needle, needle_size) == 0) { return (void*)hit; }
    h = hit + 1;
  }
  return NULL;
}

/// The case-blind compare the str* helpers use (platform/common.h): msvc's C
/// library names it with an underscore.
#define _sppc_strcasecmp _stricmp

/// The case-blind search they use, which neither of Windows' C libraries
/// has: the first place the needle matches, the haystack itself for an
/// empty one, NULL for none.
_gnu_inline _gnu_nonnull(1, 2)
char* _sppc_strcasestr(char const *haystack, char const *needle) {
  const size_t n = strlen(needle);
  for (char const *at = haystack;; ++at) {
    if (_strnicmp(at, needle, n) == 0) { return (char*)at; }
    if (*at == '\0') { return NULL; }
  }
}

// ==================== TIME ====================

_gnu_inline _gnu_restrict_access(write_only, 2) _gnu_nonnull(2)
_sppc_api int sppc_clock_gettime(const int clock_id, sppc_timespec *_sppc_restrict out_tp) {
  clockid_t os_clock;
  if (_sppc_to_os_clock(clock_id, &os_clock) != 0) { return SPPC_EINVAL; }
  struct timespec ts;
  _extract_err(int) clock_gettime(os_clock, &ts);
  if (err == 0) { _sppc_from_os_timespec(&ts, out_tp); }
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

/// As on POSIX. A task's sleep waits on the thread's completion port
/// (async/iocp.h), the thread's other tasks running meanwhile; anywhere else
/// (no port, the blocking backend) it holds the thread for the duration.
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

// ==================== THREADING ====================

/// Give the cpu up to another thread ready to run on it, if there is one.
_gnu_inline
_sppc_api void sppc_sched_yield(void) {
  SwitchToThread();
}

// ==================== FD TABLE ====================

// POSIX names an open file by a small int; Windows by a HANDLE (or, for a
// socket, a SOCKET), and its C library's own fds cover neither sockets nor
// sppc's needs, so sppc keeps its own table: fd -> open file description.
//
// As on POSIX, the description is what "dup" shares: the handle, so the file
// position goes with it, its kind and its access mode. A description is
// counted, and its handle closed when the last fd naming it is (and no call
// is still using it). fds 0, 1 and 2 start as the process's standard
// handles, and whatever is put in their place becomes the standard handle
// too, as "dup2" onto them redirects a POSIX process's.
//
// A console is a kind of its own: sppc's text is UTF-8, which a console
// takes only once its code page is changed for the whole session, so its
// output goes through "WriteConsoleW" and its input "ReadConsoleW", with
// UTF-8 converted each way here.

// mingw's C library names them; msvc's does not.
#ifndef STDIN_FILENO
  #define STDIN_FILENO 0
  #define STDOUT_FILENO 1
  #define STDERR_FILENO 2
#endif

/// The most fds a process may have open at once, as Linux's default soft
/// limit is generous; a "dup" or an open past it is EMFILE.
#define _SPPC_FD_MAX 65536

/// The largest single read or write handed to Windows, whose calls count in
/// a DWORD; a bigger one is done in part, as a POSIX read or write may be.
#define _SPPC_FD_IO_MAX ((DWORD)1 << 30)

enum {
  _SPPC_FD_HANDLE = 0, // a file or a pipe: bytes as they are
  _SPPC_FD_CONSOLE_IN, // a console's input: UTF-16 in, UTF-8 out
  _SPPC_FD_CONSOLE_OUT, // a console's screen: UTF-8 in, UTF-16 out
  _SPPC_FD_SOCKET, // a Winsock socket, its SOCKET kept as the handle
};

/// What it may be used for, as POSIX's O_ACCMODE: reading through a write
/// end is EBADF, before Windows is asked.
enum {
  _SPPC_FD_READ = 1,
  _SPPC_FD_WRITE = 2,
};

typedef struct _sppc_ofd {
  volatile LONG refs;
  int kind;
  int access;
  HANDLE handle;

  // A file on a disk has a position, which "pread" and "pwrite" borrow (a
  // positioned call on a handle like this moves it) and put back while
  // holding "lock" alone; its plain reads and writes hold "lock" shared.
  bool disk;
  // Status flags, as POSIX keeps per description: O_APPEND puts every write
  // at the end; O_NONBLOCK is kept for F_GETFL, a disk file never blocking.
  bool append;
  bool nonblock;
  // A socket's SO_REUSEADDR, kept rather than given to Windows (SOCKETS).
  bool reuseaddr;
  // The runtime whose completion port the socket is tied to, for good (0
  // for none yet; -1 for none ever): async/iocp.h's "_gt_iocp_attach".
  volatile LONG64 iocp;

  // A console's conversion state, under "lock": the start of a UTF-8
  // character a write ended part-way through, and input already read and
  // converted but not yet asked for.
  SRWLOCK lock;
  unsigned char pending[4];
  unsigned npending;
  char *in;
  size_t in_len, in_pos;
} _sppc_ofd;

typedef struct _sppc_fd_slot {
  _sppc_ofd *ofd;
  bool cloexec;
} _sppc_fd_slot;

// Defined once, in sppc.c.
extern SRWLOCK _sppc_fds_lock;
extern _sppc_fd_slot *_sppc_fds;
extern int _sppc_fds_cap;
extern INIT_ONCE _sppc_fds_once;

// ==================== FD TABLE: DESCRIPTIONS ====================

/// A console's screen or keyboard, or anything else. A console handle is
/// one "GetConsoleMode" accepts, and an input one has input events to count.
_gnu_inline
int _sppc_fd_kind_of(HANDLE handle) {
  DWORD mode, events;
  if (GetFileType(handle) != FILE_TYPE_CHAR || !GetConsoleMode(handle, &mode)) { return _SPPC_FD_HANDLE; }
  return GetNumberOfConsoleInputEvents(handle, &events) ? _SPPC_FD_CONSOLE_IN : _SPPC_FD_CONSOLE_OUT;
}

/// A new description of "handle", which it then owns; NULL (with the handle
/// left open) if there is no memory for one.
_gnu_inline
_sppc_ofd* _sppc_ofd_new(HANDLE handle, const int access) {
  _sppc_ofd *const ofd = (_sppc_ofd*)calloc(1, sizeof *ofd);
  if (ofd == NULL) { return NULL; }
  ofd->refs = 1;
  ofd->kind = _sppc_fd_kind_of(handle);
  ofd->access = access;
  ofd->handle = handle;
  ofd->disk = GetFileType(handle) == FILE_TYPE_DISK;
  InitializeSRWLock(&ofd->lock);
  return ofd;
}

_gnu_inline _gnu_nonnull(1)
void _sppc_ofd_retain(_sppc_ofd *ofd) {
  InterlockedIncrement(&ofd->refs);
}

/// Let go of a description, closing its handle with the last reference.
_gnu_inline
void _sppc_ofd_release(_sppc_ofd *ofd) {
  if (ofd == NULL || InterlockedDecrement(&ofd->refs) != 0) { return; }
  if (ofd->kind == _SPPC_FD_SOCKET) { closesocket((SOCKET)(uintptr_t)ofd->handle); }
  else { CloseHandle(ofd->handle); }
  free(ofd->in);
  free(ofd);
}

// ==================== FD TABLE: THE TABLE ====================

/// Whichever standard handle fd 0, 1 or 2 is.
_gnu_inline
DWORD _sppc_fd_std(const int fd) {
  return fd == 0 ? STD_INPUT_HANDLE : fd == 1 ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE;
}

/// fds 0, 1 and 2 from the process's standard handles, each as it can be
/// used, and none where there is none (a program with no console, say). The
/// same handle twice (stdout and stderr both the console, usually) is one
/// description, as a POSIX shell's "2>&1" makes them.
_gnu_inline_va
BOOL CALLBACK _sppc_fds_init(PINIT_ONCE once, PVOID param, PVOID *context) {
  (void)once;
  (void)param;
  (void)context;
  _sppc_fds = (_sppc_fd_slot*)calloc(16, sizeof *_sppc_fds);
  if (_sppc_fds == NULL) { return FALSE; }
  _sppc_fds_cap = 16;
  for (int fd = 0; fd < 3; ++fd) {
    const HANDLE h = GetStdHandle(_sppc_fd_std(fd));
    if (h == NULL || h == INVALID_HANDLE_VALUE) { continue; }
    for (int earlier = 0; earlier < fd; ++earlier) {
      if (_sppc_fds[earlier].ofd != NULL && _sppc_fds[earlier].ofd->handle == h) {
        _sppc_fds[fd].ofd = _sppc_fds[earlier].ofd;
        _sppc_ofd_retain(_sppc_fds[fd].ofd);
        break;
      }
    }
    // Either way, as a terminal's fds are on POSIX: whichever one a handle
    // cannot do, Windows refuses.
    if (_sppc_fds[fd].ofd == NULL) { _sppc_fds[fd].ofd = _sppc_ofd_new(h, _SPPC_FD_READ | _SPPC_FD_WRITE); }
  }
  return TRUE;
}

/// Whether the table is up (it starts on first use, so fds 0-2 work before
/// "sppc_init" as they do on POSIX).
_gnu_inline
bool _sppc_fds_ready(void) {
  return InitOnceExecuteOnce(&_sppc_fds_once, _sppc_fds_init, NULL, NULL) != FALSE;
}

/// The description "fd" names, with a reference the caller lets go of
/// ("_sppc_ofd_release") when done with it, so a close on another thread
/// cannot free it mid-call; NULL for an fd that names none (EBADF).
_gnu_inline
_sppc_ofd* _sppc_fd_get(const int fd) {
  if (fd < 0 || !_sppc_fds_ready()) { return NULL; }
  _sppc_ofd *ofd = NULL;
  AcquireSRWLockShared(&_sppc_fds_lock);
  if (fd < _sppc_fds_cap && _sppc_fds[fd].ofd != NULL) {
    ofd = _sppc_fds[fd].ofd;
    _sppc_ofd_retain(ofd);
  }
  ReleaseSRWLockShared(&_sppc_fds_lock);
  return ofd;
}

/// Room for "fd" in the table, under its exclusive lock.
_gnu_inline
bool _sppc_fds_reserve(const int fd) {
  if (fd < _sppc_fds_cap) { return true; }
  int cap = _sppc_fds_cap;
  while (cap <= fd) { cap *= 2; }
  if (cap > _SPPC_FD_MAX) { cap = _SPPC_FD_MAX; }
  _sppc_fd_slot *const grown = (_sppc_fd_slot*)realloc(_sppc_fds, (size_t)cap * sizeof *grown);
  if (grown == NULL) { return false; }
  memset(grown + _sppc_fds_cap, 0, (size_t)(cap - _sppc_fds_cap) * sizeof *grown);
  _sppc_fds = grown;
  _sppc_fds_cap = cap;
  return true;
}

/// Put "ofd" (or nothing) at "fd", under the table's exclusive lock, and hand
/// back what was there for the caller to release once the lock is let go.
/// fds 0-2 are the process's standard handles too.
_gnu_inline
_sppc_ofd* _sppc_fds_set(const int fd, _sppc_ofd *ofd, const bool cloexec) {
  _sppc_ofd *const old = _sppc_fds[fd].ofd;
  _sppc_fds[fd].ofd = ofd;
  _sppc_fds[fd].cloexec = cloexec;
  if (fd < 3) { SetStdHandle(_sppc_fd_std(fd), ofd != NULL ? ofd->handle : NULL); }
  return old;
}

/// Give "ofd" (whose reference passes to the table) the lowest free fd from
/// "lowest" up, into "out". SPPC_EMFILE once every fd up to _SPPC_FD_MAX is
/// taken, SPPC_ENOMEM without room to grow; either way the reference is let
/// go of, as the caller has no fd to close it by.
_gnu_inline _gnu_nonnull(1, 4)
int _sppc_fd_install(_sppc_ofd *ofd, const int lowest, const bool cloexec, int *_sppc_restrict out) {
  if (!_sppc_fds_ready()) {
    _sppc_ofd_release(ofd);
    return SPPC_ENOMEM;
  }
  int err = SPPC_EMFILE;
  AcquireSRWLockExclusive(&_sppc_fds_lock);
  for (int fd = lowest; fd < _SPPC_FD_MAX; ++fd) {
    if (fd < _sppc_fds_cap && _sppc_fds[fd].ofd != NULL) { continue; }
    if (!_sppc_fds_reserve(fd)) {
      err = SPPC_ENOMEM;
      break;
    }
    _sppc_fds_set(fd, ofd, cloexec);
    *out = fd;
    err = 0;
    break;
  }
  ReleaseSRWLockExclusive(&_sppc_fds_lock);
  if (err != 0) { _sppc_ofd_release(ofd); }
  return err;
}

/// A new fd for a handle sppc has just opened, which it then owns: closed
/// again if it cannot have an fd. "out" is -1 on failure.
_gnu_inline _gnu_nonnull(4)
int _sppc_fd_adopt(HANDLE handle, const int access, const bool cloexec, int *_sppc_restrict out) {
  *out = -1;
  _sppc_ofd *const ofd = _sppc_ofd_new(handle, access);
  if (ofd == NULL) {
    CloseHandle(handle);
    return SPPC_ENOMEM;
  }
  return _sppc_fd_install(ofd, 0, cloexec, out);
}

// ==================== FD TABLE: CONSOLES ====================

/// How long the UTF-8 character "lead" starts is, 1 for a byte that starts
/// none (a stray continuation byte, which converts as U+FFFD on its own).
_gnu_inline
unsigned _sppc_utf8_len(const unsigned char lead) {
  if (lead >= 0xF0 && lead <= 0xF7) { return 4; }
  if (lead >= 0xE0) { return lead <= 0xEF ? 3 : 1; }
  if (lead >= 0xC0) { return 2; }
  return 1;
}

/// How many bytes of "data" end in characters it has whole: all of them,
/// less any character the last few bytes start but do not finish.
_gnu_inline
size_t _sppc_utf8_whole(unsigned char const *data, const size_t n) {
  for (size_t back = 1; back <= 3 && back <= n; ++back) {
    const unsigned char c = data[n - back];
    if ((c & 0xC0) == 0x80) { continue; } // a continuation: keep looking for its lead
    return _sppc_utf8_len(c) > back ? n - back : n;
  }
  return n;
}

/// UTF-8 to the console, as UTF-16. 0, or the Win32 error.
_gnu_inline
DWORD _sppc_console_put(HANDLE handle, unsigned char const *data, const size_t n) {
  if (n == 0) { return 0; }
  const int wide = MultiByteToWideChar(CP_UTF8, 0, (char const*)data, (int)n, NULL, 0);
  if (wide <= 0) { return GetLastError(); }
  wchar_t *const w = (wchar_t*)malloc((size_t)wide * sizeof *w);
  if (w == NULL) { return ERROR_NOT_ENOUGH_MEMORY; }
  MultiByteToWideChar(CP_UTF8, 0, (char const*)data, (int)n, w, wide);
  DWORD err = 0;
  for (int done = 0; done < wide;) {
    DWORD wrote = 0;
    if (!WriteConsoleW(handle, w + done, (DWORD)(wide - done), &wrote, NULL)) {
      err = GetLastError();
      break;
    }
    done += (int)wrote;
  }
  free(w);
  return err;
}

/// A write to a console: every byte of "data" is taken, though a character
/// it ends part-way through is only shown once a later write finishes it.
/// The number of bytes taken, or -1 with the Win32 error in "err".
_gnu_inline _gnu_nonnull(1, 4)
ssize_t _sppc_console_write(_sppc_ofd *ofd, unsigned char const *data, size_t n, DWORD *err) {
  const size_t total = n;
  AcquireSRWLockExclusive(&ofd->lock);
  *err = 0;

  // Finish the character the last write began.
  if (ofd->npending > 0 && n > 0) {
    const unsigned want = _sppc_utf8_len(ofd->pending[0]);
    while (ofd->npending < want && n > 0 && (*data & 0xC0) == 0x80) {
      ofd->pending[ofd->npending++] = *data++;
      --n;
    }
    if (ofd->npending == want || n > 0) {
      // Whole, or cut short by a byte that cannot continue it: either way out it goes.
      *err = _sppc_console_put(ofd->handle, ofd->pending, ofd->npending);
      ofd->npending = 0;
    }
  }

  if (*err == 0) {
    const size_t whole = _sppc_utf8_whole(data, n);
    *err = _sppc_console_put(ofd->handle, data, whole);
    if (*err == 0) {
      memcpy(ofd->pending + ofd->npending, data + whole, n - whole);
      ofd->npending += (unsigned)(n - whole);
    }
  }
  ReleaseSRWLockExclusive(&ofd->lock);
  return *err == 0 ? (ssize_t)total : -1;
}

/// A read from a console: a line typed into it (as a terminal gives one),
/// in UTF-8, ending in "\n" rather than the console's "\r\n". A line longer
/// than "n" is handed out over as many reads as it takes. Ctrl+Z at the start
/// of a line is the end of input, as Windows has it. The number of bytes,
/// 0 at the end of input, or -1 with the Win32 error in "err".
_gnu_inline _gnu_nonnull(1, 2, 4)
ssize_t _sppc_console_read(_sppc_ofd *ofd, char *out, const size_t n, DWORD *err) {
  AcquireSRWLockExclusive(&ofd->lock);
  *err = 0;
  ssize_t result = 0;
  if (ofd->in_pos == ofd->in_len && n > 0) {
    wchar_t line[4096];
    DWORD got = 0;
    if (!ReadConsoleW(ofd->handle, line, sizeof line / sizeof *line, &got, NULL)) {
      *err = GetLastError();
      result = -1;
      goto done;
    }
    if (got == 0 || line[0] == 0x1A) { goto done; }
    DWORD kept = 0;
    for (DWORD i = 0; i < got; ++i) {
      if (line[i] == L'\r' && i + 1 < got && line[i + 1] == L'\n') { continue; }
      line[kept++] = line[i];
    }
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, line, (int)kept, NULL, 0, NULL, NULL);
    char *const in = (char*)realloc(ofd->in, (size_t)(bytes > 0 ? bytes : 1));
    if (in == NULL) {
      *err = ERROR_NOT_ENOUGH_MEMORY;
      result = -1;
      goto done;
    }
    ofd->in = in;
    ofd->in_len = (size_t)WideCharToMultiByte(CP_UTF8, 0, line, (int)kept, in, bytes, NULL, NULL);
    ofd->in_pos = 0;
  }
  {
    const size_t left = ofd->in_len - ofd->in_pos;
    const size_t take = left < n ? left : n;
    memcpy(out, ofd->in + ofd->in_pos, take);
    ofd->in_pos += take;
    result = (ssize_t)take;
  }
done:
  ReleaseSRWLockExclusive(&ofd->lock);
  return result;
}

// ==================== FD TABLE: COMPLETION PORT ====================

// A blocking socket's sends, receives, accepts and connects go through the
// thread's completion port (async/iocp.h) where they can, so a task waiting
// on one lets the thread's other tasks run, as on Linux's ring. Where they
// cannot (no port, a non-blocking socket, one tied to another thread's
// port) they are the plain calls, which block the thread.

/// Whether an operation on the socket of "ofd" may wait on this thread's
/// port, tying the socket to it if no thread's port has it yet.
_gnu_inline _gnu_nonnull(1)
bool _sppc_win_on_port(_sppc_ofd *ofd) {
#if SPPC_ASYNC_IOCP
  return !ofd->nonblock && _gt_iocp_attach(ofd->handle, &ofd->iocp);
#else
  (void)ofd;
  return false;
#endif
}

/// One send or receive on the socket of "ofd", as Winsock's send / recv (or
/// sendto / recvfrom, given an address and its length): the count, or
/// SOCKET_ERROR with Winsock's error in "win". "port" says whether it may
/// go through the port (not for MSG_DONTWAIT, which must not wait at all).
_gnu_inline _gnu_nonnull(1, 2, 8)
int _sppc_win_transfer(_sppc_ofd *ofd, char *buffer, const int n, const int flags, const bool sending,
  struct sockaddr *addr, int *addr_len, int *win, const bool port) {
  const SOCKET socket = (SOCKET)(uintptr_t)ofd->handle;
  *win = 0;
#if SPPC_ASYNC_IOCP
  if (port && _sppc_win_on_port(ofd)) {
    WSABUF buf;
    buf.len = (ULONG)n;
    buf.buf = buffer;
    DWORD how = (DWORD)flags;
    OVERLAPPED *const ov = _gt_iocp_overlapped();
    const int r = sending
      ? (addr != NULL ? WSASendTo(socket, &buf, 1, NULL, how, addr, *addr_len, ov, NULL)
        : WSASend(socket, &buf, 1, NULL, how, ov, NULL))
      : (addr != NULL ? WSARecvFrom(socket, &buf, 1, NULL, &how, addr, addr_len, ov, NULL)
        : WSARecv(socket, &buf, 1, NULL, &how, ov, NULL));
    if (r == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
      *win = WSAGetLastError();
      return SOCKET_ERROR;
    }
    _gt_iocp_wait(ofd->handle);
    DWORD done = 0;
    if (!WSAGetOverlappedResult(socket, ov, &done, FALSE, &how)) {
      *win = WSAGetLastError();
      return SOCKET_ERROR;
    }
    return (int)done;
  }
#else
  (void)port;
#endif
  const int r = sending
    ? (addr != NULL ? sendto(socket, buffer, n, flags, addr, *addr_len) : send(socket, buffer, n, flags))
    : (addr != NULL ? recvfrom(socket, buffer, n, flags, addr, addr_len) : recv(socket, buffer, n, flags));
  if (r == SOCKET_ERROR) { *win = WSAGetLastError(); }
  return r;
}

// ==================== FD TABLE: I/O ====================

/// Read into "buffer" from a description, as POSIX's "read": the number of
/// bytes, 0 at the end of a file or of a pipe whose writers have all gone,
/// or -1 with the sppc error in "err".
_gnu_inline _gnu_nonnull(1, 2, 4)
ssize_t _sppc_ofd_read(_sppc_ofd *ofd, void *buffer, const size_t n, int *err) {
  *err = 0;
  if ((ofd->access & _SPPC_FD_READ) == 0) {
    *err = SPPC_EBADF;
    return -1;
  }
  if (ofd->kind == _SPPC_FD_SOCKET) {
    int win;
    const int got = _sppc_win_transfer(ofd, (char*)buffer, n < INT_MAX ? (int)n : INT_MAX, 0, false, NULL, NULL, &win, true);
    if (got == SOCKET_ERROR) {
      *err = win == WSAECONNABORTED ? SPPC_ECONNRESET : _sppc_errno_wsa(win); // as POSIX's receiver
      return -1;
    }
    return got;
  }
  DWORD win = 0;
  if (ofd->kind == _SPPC_FD_CONSOLE_IN) {
    const ssize_t got = _sppc_console_read(ofd, (char*)buffer, n, &win);
    if (got < 0) { *err = _sppc_errno_win32(win); }
    return got;
  }
  if (ofd->kind == _SPPC_FD_CONSOLE_OUT) {
    *err = SPPC_EBADF;
    return -1;
  }
  DWORD got = 0;
  if (ofd->disk) { AcquireSRWLockShared(&ofd->lock); }
  const BOOL ok = ReadFile(ofd->handle, buffer, n < _SPPC_FD_IO_MAX ? (DWORD)n : _SPPC_FD_IO_MAX, &got, NULL);
  win = ok ? 0 : GetLastError();
  if (ofd->disk) { ReleaseSRWLockShared(&ofd->lock); }
  if (!ok) {
    if (win == ERROR_BROKEN_PIPE || win == ERROR_HANDLE_EOF) { return 0; }
    // A directory opened for reading has nothing a read can give, as on Linux.
    *err = win == ERROR_INVALID_FUNCTION && ofd->disk ? SPPC_EISDIR : _sppc_errno_win32(win);
    return -1;
  }
  return (ssize_t)got;
}

/// Write "buffer" to a description, as POSIX's "write": the number of bytes
/// taken, or -1 with the sppc error in "err" (EPIPE for a pipe whose readers
/// have all gone).
_gnu_inline _gnu_nonnull(1, 2, 4)
ssize_t _sppc_ofd_write(_sppc_ofd *ofd, void const *buffer, const size_t n, int *err) {
  *err = 0;
  if ((ofd->access & _SPPC_FD_WRITE) == 0 || ofd->kind == _SPPC_FD_CONSOLE_IN) {
    *err = SPPC_EBADF;
    return -1;
  }
  if (ofd->kind == _SPPC_FD_SOCKET) {
    int win;
    const int sent = _sppc_win_transfer(ofd, (char*)(uintptr_t)buffer, n < INT_MAX ? (int)n : INT_MAX, 0, true, NULL, NULL,
      &win, true);
    if (sent == SOCKET_ERROR) {
      *err = win == WSAECONNABORTED ? SPPC_EPIPE : _sppc_errno_wsa(win); // as POSIX's sender
      return -1;
    }
    return sent;
  }
  DWORD win = 0;
  if (ofd->kind == _SPPC_FD_CONSOLE_OUT) {
    const ssize_t took = _sppc_console_write(ofd, (unsigned char const*)buffer, n, &win);
    if (took < 0) { *err = _sppc_errno_win32(win); }
    return took;
  }
  // O_APPEND: an offset of all ones is Windows' "at the end", taken as one
  // step with the write, as POSIX's append is.
  OVERLAPPED at_end;
  memset(&at_end, 0, sizeof at_end);
  at_end.Offset = at_end.OffsetHigh = 0xFFFFFFFF;
  DWORD wrote = 0;
  if (ofd->disk) { AcquireSRWLockShared(&ofd->lock); }
  const BOOL ok = WriteFile(ofd->handle, buffer, n < _SPPC_FD_IO_MAX ? (DWORD)n : _SPPC_FD_IO_MAX, &wrote,
    ofd->append ? &at_end : NULL);
  win = ok ? 0 : GetLastError();
  if (ofd->disk) { ReleaseSRWLockShared(&ofd->lock); }
  if (!ok) {
    *err = _sppc_errno_win32(win);
    return -1;
  }
  return (ssize_t)wrote;
}

/// "pread" / "pwrite" on a description: at "offset", leaving its position
/// where it was. Only a file on a disk has positions (ESPIPE for anything
/// else); the position is put back with the description's lock held alone,
/// so no plain read or write on another thread sees it moved.
_gnu_inline _gnu_nonnull(1, 2, 6)
ssize_t _sppc_ofd_pio(_sppc_ofd *ofd, void *buffer, const size_t n, const int64_t offset, const bool write, int *err) {
  *err = 0;
  if ((ofd->access & (write ? _SPPC_FD_WRITE : _SPPC_FD_READ)) == 0) {
    *err = SPPC_EBADF;
    return -1;
  }
  if (!ofd->disk) {
    *err = SPPC_ESPIPE;
    return -1;
  }
  if (offset < 0) {
    *err = SPPC_EINVAL;
    return -1;
  }
  OVERLAPPED at;
  memset(&at, 0, sizeof at);
  at.Offset = (DWORD)((uint64_t)offset & 0xFFFFFFFFu);
  at.OffsetHigh = (DWORD)((uint64_t)offset >> 32);
  const DWORD want = n < _SPPC_FD_IO_MAX ? (DWORD)n : _SPPC_FD_IO_MAX;
  LARGE_INTEGER zero, was;
  zero.QuadPart = 0;
  DWORD done = 0;
  AcquireSRWLockExclusive(&ofd->lock);
  SetFilePointerEx(ofd->handle, zero, &was, FILE_CURRENT);
  const BOOL ok = write ? WriteFile(ofd->handle, buffer, want, &done, &at) : ReadFile(ofd->handle, buffer, want, &done, &at);
  const DWORD win = ok ? 0 : GetLastError();
  SetFilePointerEx(ofd->handle, was, NULL, FILE_BEGIN);
  ReleaseSRWLockExclusive(&ofd->lock);
  if (!ok) {
    if (!write && win == ERROR_HANDLE_EOF) { return 0; }
    *err = _sppc_errno_win32(win);
    return -1;
  }
  return (ssize_t)done;
}

// ==================== FILE DESCRIPTORS ====================

// Over the fd table above. Windows has only the blocking i/o
// backend so far, so a task's read or write holds its whole thread.

_gnu_inline _gnu_restrict_access(write_only, 1) _gnu_restrict_access(write_only, 5) _gnu_nonnull(1, 5)
_sppc_api int sppc_read(char *_sppc_restrict buffer, const size_t size, const size_t count, const int fd,
  ssize_t *_sppc_restrict out_n) {
  *out_n = -1;
  _sppc_ofd *const ofd = _sppc_fd_get(fd);
  if (ofd == NULL) { return SPPC_EBADF; }
  int err = 0;
  *out_n = _sppc_ofd_read(ofd, buffer, size * count, &err);
  _sppc_ofd_release(ofd);
  return err;
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(write_only, 5) _gnu_nonnull(1, 5)
_sppc_api int sppc_write(char const *_sppc_restrict buffer, const size_t size, const size_t count, const int fd,
  ssize_t *_sppc_restrict out_n) {
  *out_n = -1;
  _sppc_ofd *const ofd = _sppc_fd_get(fd);
  if (ofd == NULL) { return SPPC_EBADF; }
  int err = 0;
  *out_n = _sppc_ofd_write(ofd, buffer, size * count, &err);
  _sppc_ofd_release(ofd);
  return err;
}

/// The fd goes at once; its handle once no other fd names its description
/// and no call still has it.
_gnu_inline
_sppc_api int sppc_close(const int fd) {
  if (fd < 0 || !_sppc_fds_ready()) { return SPPC_EBADF; }
  _sppc_ofd *old = NULL;
  AcquireSRWLockExclusive(&_sppc_fds_lock);
  const bool open = fd < _sppc_fds_cap && _sppc_fds[fd].ofd != NULL;
  if (open) { old = _sppc_fds_set(fd, NULL, false); }
  ReleaseSRWLockExclusive(&_sppc_fds_lock);
  if (!open) { return SPPC_EBADF; }
  _sppc_ofd_release(old);
  return 0;
}

/// The lowest free fd, naming the same description: one position, shared.
_gnu_inline _gnu_restrict_access(write_only, 2) _gnu_nonnull(2)
_sppc_api int sppc_dup(const int fd, int *_sppc_restrict out_fd) {
  *out_fd = -1;
  _sppc_ofd *const ofd = _sppc_fd_get(fd);
  if (ofd == NULL) { return SPPC_EBADF; }
  return _sppc_fd_install(ofd, 0, false, out_fd);
}

/// "target_fd" made to name what "fd" does, closing what it named before;
/// onto itself, nothing (for an fd that is open).
_gnu_inline
_sppc_api int sppc_dup2(const int fd, const int target_fd) {
  _sppc_ofd *const ofd = _sppc_fd_get(fd);
  if (ofd == NULL) { return SPPC_EBADF; }
  if (target_fd < 0 || target_fd >= _SPPC_FD_MAX) {
    _sppc_ofd_release(ofd);
    return SPPC_EBADF;
  }
  if (target_fd == fd) {
    _sppc_ofd_release(ofd);
    return 0;
  }
  _sppc_ofd *old = NULL;
  AcquireSRWLockExclusive(&_sppc_fds_lock);
  const bool room = _sppc_fds_reserve(target_fd);
  if (room) { old = _sppc_fds_set(target_fd, ofd, false); }
  ReleaseSRWLockExclusive(&_sppc_fds_lock);
  if (!room) {
    _sppc_ofd_release(ofd);
    return SPPC_ENOMEM;
  }
  _sppc_ofd_release(old);
  return 0;
}

/// An anonymous pipe, both ends close-on-exec (not inherited) as on POSIX,
/// with Linux's 64 KiB of buffer.
_gnu_inline _gnu_restrict_access(write_only, 1) _gnu_restrict_access(write_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_pipe(int *_sppc_restrict out_read_fd, int *_sppc_restrict out_write_fd) {
  HANDLE r, w;
  if (!CreatePipe(&r, &w, NULL, 65536)) { return _sppc_errno_win32(GetLastError()); }
  int rfd = -1, wfd = -1;
  const int rerr = _sppc_fd_adopt(r, _SPPC_FD_READ, true, &rfd);
  if (rerr != 0) {
    CloseHandle(w);
    return rerr;
  }
  const int werr = _sppc_fd_adopt(w, _SPPC_FD_WRITE, true, &wfd);
  if (werr != 0) {
    sppc_close(rfd);
    return werr;
  }
  *out_read_fd = rfd;
  *out_write_fd = wfd;
  return 0;
}

// ==================== PATHS ====================

// Paths are Windows' own, as the S++ stdlib's Path has them there: a drive
// ("C:\"), a UNC share or a relative path, with either separator, which
// Windows takes both of. They cross as UTF-8 and are UTF-16 to Windows (its
// "W" calls); a path sppc hands back (getcwd, realpath, readlink) is in
// Windows' own form, with backslashes.

/// The most a path sppc fills in without being told the room ("sppc_realpath")
/// may take, terminator included: Windows' MAX_PATH, the stdlib's "path_max".
#define _SPPC_WIN_PATH_MAX MAX_PATH

/// "path" as UTF-16, in a block the caller frees: ENOENT for an empty path,
/// as POSIX has it, and EILSEQ for bytes that are not UTF-8.
_gnu_inline _gnu_nonnull(1, 2)
int _sppc_win_wpath(char const *path, wchar_t **out) {
  *out = NULL;
  if (*path == '\0') { return SPPC_ENOENT; }
  const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
  if (n <= 0) { return SPPC_EILSEQ; }
  wchar_t *const w = (wchar_t*)malloc((size_t)n * sizeof *w);
  if (w == NULL) { return SPPC_ENOMEM; }
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, w, n);
  *out = w;
  return 0;
}

/// A path of Windows' back as UTF-8, terminated, into "buffer" of "size":
/// without the "\\?\" a final path name starts with ("\\?\UNC\" becomes the
/// "\\" of a share). "too_long" when it does not fit, the buffer emptied.
_gnu_inline _gnu_nonnull(1)
int _sppc_win_utf8_path(wchar_t const *w, char *buffer, const size_t size, const int too_long) {
  bool unc = false;
  if (wcsncmp(w, L"\\\\?\\UNC\\", 8) == 0) {
    w += 8;
    unc = true;
  } else if (wcsncmp(w, L"\\\\?\\", 4) == 0) {
    w += 4;
  }
  const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
  const size_t need = (size_t)(n > 0 ? n : 0) + (unc ? 2 : 0);
  if (n <= 0 || need > size) {
    if (size > 0) { buffer[0] = '\0'; }
    return n <= 0 ? SPPC_EILSEQ : too_long;
  }
  if (unc) {
    buffer[0] = '\\';
    buffer[1] = '\\';
  }
  WideCharToMultiByte(CP_UTF8, 0, w, -1, buffer + (unc ? 2 : 0), n, NULL, NULL);
  return 0;
}

_gnu_inline
bool _sppc_win_is_sep(const wchar_t c) {
  return c == L'\\' || c == L'/';
}

/// The sppc error for "err" from a call on "path": as "_sppc_errno_win32",
/// except where Windows calls a path missing that has a file, not a
/// directory, on the way to it, which POSIX calls ENOTDIR.
_gnu_inline _gnu_nonnull(1)
int _sppc_win_path_errno(wchar_t const *path, const DWORD err) {
  const int e = _sppc_errno_win32(err);
  if (e != SPPC_ENOENT) { return e; }
  size_t len = wcslen(path);
  wchar_t *const p = (wchar_t*)malloc((len + 1) * sizeof *p);
  if (p == NULL) { return e; }
  memcpy(p, path, (len + 1) * sizeof *p);
  int out = e;
  for (;;) {
    while (len > 0 && _sppc_win_is_sep(p[len - 1])) { --len; }
    while (len > 0 && !_sppc_win_is_sep(p[len - 1])) { --len; }
    while (len > 0 && _sppc_win_is_sep(p[len - 1])) { --len; }
    if (len == 0 || (len == 2 && p[1] == L':')) { break; }
    p[len] = L'\0';
    const DWORD attrs = GetFileAttributesW(p);
    if (attrs != INVALID_FILE_ATTRIBUTES) {
      if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) { out = SPPC_ENOTDIR; }
      break;
    }
  }
  free(p);
  return out;
}

/// A handle on "path" itself, directories included, following a symbolic
/// link at its end or not, for "access". Shared every way, so it stands in
/// no one's way, as POSIX's descriptors do not.
_gnu_inline _gnu_nonnull(1)
HANDLE _sppc_win_open(wchar_t const *path, const DWORD access, const bool follow) {
  return CreateFileW(path, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
    FILE_FLAG_BACKUP_SEMANTICS | (follow ? 0 : FILE_FLAG_OPEN_REPARSE_POINT), NULL);
}

/// Whether "h" (opened without following) is a symbolic link. Other reparse
/// points (junctions, cloud placeholders) are what they stand for.
_gnu_inline
bool _sppc_win_is_link(HANDLE h) {
  FILE_ATTRIBUTE_TAG_INFO tag;
  return GetFileInformationByHandleEx(h, FileAttributeTagInfo, &tag, sizeof tag)
    && (tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 && tag.ReparseTag == IO_REPARSE_TAG_SYMLINK;
}

/// What a symbolic link's reparse point holds (Windows' SDK has it only in
/// the driver kit).
typedef struct _sppc_win_reparse {
  ULONG tag;
  USHORT data_length;
  USHORT reserved;
  USHORT substitute_offset;
  USHORT substitute_length;
  USHORT print_offset;
  USHORT print_length;
  ULONG flags;
  WCHAR names[1];
} _sppc_win_reparse;

/// The target a symbolic link "h" holds, as it was given: UTF-16, in a block
/// the caller frees. EINVAL for anything that is not a symbolic link, as
/// POSIX's readlink has it.
_gnu_inline _gnu_nonnull(2)
int _sppc_win_link_target(HANDLE h, wchar_t **out) {
  *out = NULL;
  _sppc_win_reparse *const data = (_sppc_win_reparse*)malloc(MAXIMUM_REPARSE_DATA_BUFFER_SIZE);
  if (data == NULL) { return SPPC_ENOMEM; }
  DWORD got = 0;
  int err = 0;
  if (!DeviceIoControl(h, FSCTL_GET_REPARSE_POINT, NULL, 0, data, MAXIMUM_REPARSE_DATA_BUFFER_SIZE, &got, NULL)) {
    const DWORD win = GetLastError();
    err = win == ERROR_NOT_A_REPARSE_POINT ? SPPC_EINVAL : _sppc_errno_win32(win);
  } else if (data->tag != IO_REPARSE_TAG_SYMLINK) {
    err = SPPC_EINVAL;
  } else {
    // The name to show is the one it was made with; the substitute is the
    // NT form ("\??\C:\..."), there to fall back on.
    USHORT offset = data->print_offset, length = data->print_length;
    if (length == 0) {
      offset = data->substitute_offset;
      length = data->substitute_length;
    }
    wchar_t const *name = (wchar_t const*)((char const*)data->names + offset);
    size_t chars = length / sizeof(wchar_t);
    if (chars >= 4 && wcsncmp(name, L"\\??\\", 4) == 0) {
      name += 4;
      chars -= 4;
    }
    wchar_t *const target = (wchar_t*)malloc((chars + 1) * sizeof *target);
    if (target == NULL) { err = SPPC_ENOMEM; }
    else {
      memcpy(target, name, chars * sizeof *target);
      target[chars] = L'\0';
      *out = target;
    }
  }
  free(data);
  return err;
}

// ==================== METADATA ====================

/// A Windows time (100 ns since 1601) as a POSIX one; before 1970 is 1970,
/// as on POSIX.
_gnu_inline _gnu_nonnull(2)
void _sppc_win_timespec(const int64_t ticks, sppc_timespec *out) {
  const int64_t since = ticks - (int64_t)_SPPC_WIN_EPOCH_TICKS;
  out->sec = since < 0 ? 0 : (uint64_t)(since / 10000000);
  out->nsec = since < 0 ? 0 : (uint64_t)(since % 10000000) * 100;
}

/// Whether "name" is a program, by the extensions Windows runs: the closest
/// it has to an executable bit.
_gnu_inline _gnu_nonnull(1)
bool _sppc_win_is_program(wchar_t const *name, const size_t len) {
  if (len < 4 || name[len - 4] != L'.') { return false; }
  wchar_t ext[4];
  for (int i = 0; i < 3; ++i) { ext[i] = (wchar_t)towlower(name[len - 3 + i]); }
  ext[3] = L'\0';
  return wcscmp(ext, L"exe") == 0 || wcscmp(ext, L"bat") == 0 || wcscmp(ext, L"cmd") == 0 || wcscmp(ext, L"com") == 0;
}

/// "h"'s metadata as POSIX's. Windows keeps no permission bits, so they are
/// made from what it does keep, as Python's os.stat makes them: a file is
/// 0666, or 0444 when read-only, with 0111 for a program; a directory is
/// 0777, or 0555; a symbolic link (when "link": "h" is the link, as lstat
/// sees it) is 0777, its size the length of its target. Every file belongs
/// to uid and gid 0. A pipe or a console has its type and nothing else.
_gnu_inline _gnu_nonnull(3)
int _sppc_win_stat_handle(HANDLE h, const bool link, struct sppc_stat *out) {
  memset(out, 0, sizeof *out);
  out->block_size = 4096;
  out->nlink = 1;
  const DWORD type = GetFileType(h);
  if (type == FILE_TYPE_CHAR) {
    out->mode = SPPC_S_IFCHR | 0666;
    return 0;
  }
  if (type == FILE_TYPE_PIPE) {
    out->mode = SPPC_S_IFIFO | 0600;
    return 0;
  }
  BY_HANDLE_FILE_INFORMATION info;
  FILE_BASIC_INFO basic;
  FILE_STANDARD_INFO standard;
  if (!GetFileInformationByHandle(h, &info) || !GetFileInformationByHandleEx(h, FileBasicInfo, &basic, sizeof basic)
    || !GetFileInformationByHandleEx(h, FileStandardInfo, &standard, sizeof standard)) {
    return _sppc_errno_win32(GetLastError());
  }
  out->dev = info.dwVolumeSerialNumber;
  out->ino = (uint64_t)info.nFileIndexHigh << 32 | info.nFileIndexLow;
  out->nlink = info.nNumberOfLinks;
  out->size = (int64_t)((uint64_t)info.nFileSizeHigh << 32 | info.nFileSizeLow);
  out->blocks = standard.AllocationSize.QuadPart / 512;
  _sppc_win_timespec(basic.LastAccessTime.QuadPart, &out->atim);
  _sppc_win_timespec(basic.LastWriteTime.QuadPart, &out->mtim);
  _sppc_win_timespec(basic.ChangeTime.QuadPart, &out->ctim);
  const bool read_only = (info.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
  if (link) {
    out->mode = SPPC_S_IFLNK | 0777;
    wchar_t *target = NULL;
    if (_sppc_win_link_target(h, &target) == 0) {
      const int bytes = WideCharToMultiByte(CP_UTF8, 0, target, -1, NULL, 0, NULL, NULL);
      out->size = bytes > 0 ? bytes - 1 : 0;
      free(target);
    }
  } else if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
    out->mode = SPPC_S_IFDIR | (read_only ? 0555 : 0777);
  } else {
    out->mode = SPPC_S_IFREG | (read_only ? 0444 : 0666);
    wchar_t name[1024];
    const DWORD len = GetFinalPathNameByHandleW(h, name, sizeof name / sizeof *name, FILE_NAME_NORMALIZED);
    if (len > 0 && len < sizeof name / sizeof *name && _sppc_win_is_program(name, len)) { out->mode |= 0111; }
  }
  return 0;
}

/// stat and lstat: "path"'s metadata, following a symbolic link at its end
/// or not.
_gnu_inline _gnu_nonnull(1, 3)
int _sppc_win_stat_path(char const *path, const bool follow, struct sppc_stat *out) {
  wchar_t *w;
  const int bad = _sppc_win_wpath(path, &w);
  if (bad != 0) { return bad; }
  const HANDLE h = _sppc_win_open(w, FILE_READ_ATTRIBUTES, follow);
  int err;
  if (h == INVALID_HANDLE_VALUE) { err = _sppc_win_path_errno(w, GetLastError()); }
  else {
    err = _sppc_win_stat_handle(h, !follow && _sppc_win_is_link(h), out);
    CloseHandle(h);
  }
  free(w);
  return err;
}

// ==================== FILES ====================

/// The open flags Windows can honour. O_PATH and O_TMPFILE it has nothing
/// like, so are refused (EINVAL); O_NOCTTY, O_ASYNC, O_DIRECT and O_NOATIME
/// mean nothing there and are taken without effect.
#define _SPPC_WIN_OPEN_FLAGS (SPPC_O_ACCMODE | SPPC_O_CREAT | SPPC_O_EXCL | SPPC_O_NOCTTY | SPPC_O_TRUNC \
  | SPPC_O_APPEND | SPPC_O_NONBLOCK | SPPC_O_SYNC | SPPC_O_DSYNC | SPPC_O_ASYNC | SPPC_O_DIRECT \
  | SPPC_O_DIRECTORY | SPPC_O_NOFOLLOW | SPPC_O_NOATIME | SPPC_O_CLOEXEC)

/// As POSIX's open. The mode matters only to a file it creates, which is
/// read-only (the attribute) without the owner's write bit; this descriptor
/// may still write to it, as POSIX's may. A directory opens for reading only
/// (EISDIR for writing), with or without O_DIRECTORY. Not CLOEXEC, a
/// descriptor is one a child would get, once sppc starts processes.
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(write_only, 4) _gnu_nonnull(1, 4)
_sppc_api int sppc_open(char const *_sppc_restrict path, const int flags, const uint32_t mode, int *_sppc_restrict out_fd) {
  *out_fd = -1;
  const int accmode = flags & SPPC_O_ACCMODE;
  if ((flags & ~_SPPC_WIN_OPEN_FLAGS) != 0 || accmode == SPPC_O_ACCMODE || (mode & ~07777u) != 0) { return SPPC_EINVAL; }
  const bool reads = accmode != SPPC_O_WRONLY, writes = accmode != SPPC_O_RDONLY;
  const bool create = (flags & SPPC_O_CREAT) != 0, exclusive = create && (flags & SPPC_O_EXCL) != 0;
  const bool truncate = (flags & SPPC_O_TRUNC) != 0;

  wchar_t *w;
  const int bad = _sppc_win_wpath(path, &w);
  if (bad != 0) { return bad; }

  // What a directory gets, before Windows answers it with "access denied".
  const DWORD attrs = GetFileAttributesW(w);
  if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) && !exclusive && (writes || truncate)) {
    free(w);
    return SPPC_EISDIR;
  }

  // A read-only descriptor still truncates (O_TRUNC), as Linux's does: the
  // handle can write, the descriptor cannot.
  DWORD access = (reads ? GENERIC_READ : 0) | (writes || truncate ? GENERIC_WRITE : 0);
  if (access == 0) { access = FILE_READ_ATTRIBUTES; }
  const DWORD disposition = exclusive ? CREATE_NEW : create ? OPEN_ALWAYS : OPEN_EXISTING;
  DWORD how = FILE_FLAG_BACKUP_SEMANTICS;
  if (flags & SPPC_O_NOFOLLOW) { how |= FILE_FLAG_OPEN_REPARSE_POINT; }
  if (flags & (SPPC_O_SYNC | SPPC_O_DSYNC)) { how |= FILE_FLAG_WRITE_THROUGH; }
  how |= create && (mode & SPPC_S_IWUSR) == 0 ? FILE_ATTRIBUTE_READONLY : FILE_ATTRIBUTE_NORMAL;

  const HANDLE h = CreateFileW(w, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, disposition,
    how, NULL);
  if (h == INVALID_HANDLE_VALUE) {
    const int err = _sppc_win_path_errno(w, GetLastError());
    free(w);
    return err;
  }
  free(w);

  int err = 0;
  BY_HANDLE_FILE_INFORMATION info;
  const bool dir = GetFileInformationByHandle(h, &info) && (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
  if ((flags & SPPC_O_NOFOLLOW) && _sppc_win_is_link(h)) { err = SPPC_ELOOP; }
  else if ((flags & SPPC_O_DIRECTORY) && !dir) { err = SPPC_ENOTDIR; }
  else if (truncate && !dir) {
    FILE_END_OF_FILE_INFO end;
    end.EndOfFile.QuadPart = 0;
    if (!SetFileInformationByHandle(h, FileEndOfFileInfo, &end, sizeof end)) { err = _sppc_errno_win32(GetLastError()); }
  }
  if (err != 0) {
    CloseHandle(h);
    return err;
  }

  _sppc_ofd *const ofd = _sppc_ofd_new(h, (reads ? _SPPC_FD_READ : 0) | (writes ? _SPPC_FD_WRITE : 0));
  if (ofd == NULL) {
    CloseHandle(h);
    return SPPC_ENOMEM;
  }
  ofd->append = (flags & SPPC_O_APPEND) != 0;
  ofd->nonblock = (flags & SPPC_O_NONBLOCK) != 0;
  return _sppc_fd_install(ofd, 0, (flags & SPPC_O_CLOEXEC) != 0, out_fd);
}

_gnu_inline _gnu_restrict_access(write_only, 2) _gnu_nonnull(2)
_sppc_api int sppc_fstat(const int fd, struct sppc_stat *_sppc_restrict out) {
  _sppc_ofd *const ofd = _sppc_fd_get(fd);
  if (ofd == NULL) { return SPPC_EBADF; }
  int err = 0;
  if (ofd->kind == _SPPC_FD_SOCKET) {
    memset(out, 0, sizeof *out);
    out->mode = SPPC_S_IFSOCK | 0777;
    out->nlink = 1;
    out->block_size = 4096;
  } else {
    err = _sppc_win_stat_handle(ofd->handle, false, out);
  }
  _sppc_ofd_release(ofd);
  return err;
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(write_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_stat(char const *_sppc_restrict path, struct sppc_stat *_sppc_restrict out) {
  return _sppc_win_stat_path(path, true, out);
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(write_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_lstat(char const *_sppc_restrict path, struct sppc_stat *_sppc_restrict out) {
  return _sppc_win_stat_path(path, false, out);
}

/// ESPIPE for anything without a position (a pipe, a console).
_gnu_inline _gnu_restrict_access(write_only, 4) _gnu_nonnull(4)
_sppc_api int sppc_lseek(const int fd, const int64_t offset, const int whence, int64_t *_sppc_restrict out_pos) {
  *out_pos = -1;
  if (whence != SPPC_SEEK_SET && whence != SPPC_SEEK_CUR && whence != SPPC_SEEK_END) { return SPPC_EINVAL; }
  _sppc_ofd *const ofd = _sppc_fd_get(fd);
  if (ofd == NULL) { return SPPC_EBADF; }
  int err = 0;
  if (!ofd->disk) { err = SPPC_ESPIPE; }
  else {
    // FILE_BEGIN, FILE_CURRENT and FILE_END are SEEK_SET, SEEK_CUR and SEEK_END.
    LARGE_INTEGER by, at;
    by.QuadPart = offset;
    AcquireSRWLockShared(&ofd->lock);
    if (SetFilePointerEx(ofd->handle, by, &at, (DWORD)whence)) { *out_pos = at.QuadPart; }
    else { err = _sppc_errno_win32(GetLastError()); }
    ReleaseSRWLockShared(&ofd->lock);
  }
  _sppc_ofd_release(ofd);
  return err;
}

_gnu_inline _gnu_restrict_access(write_only, 2) _gnu_restrict_access(write_only, 6) _gnu_nonnull(2, 6)
_sppc_api int sppc_pread(const int fd, void *_sppc_restrict buffer, const size_t size, const size_t count, const int64_t offset,
  ssize_t *_sppc_restrict out_n) {
  *out_n = -1;
  _sppc_ofd *const ofd = _sppc_fd_get(fd);
  if (ofd == NULL) { return SPPC_EBADF; }
  int err = 0;
  *out_n = _sppc_ofd_pio(ofd, buffer, size * count, offset, false, &err);
  _sppc_ofd_release(ofd);
  return err;
}

_gnu_inline _gnu_restrict_access(read_only, 2) _gnu_restrict_access(write_only, 6) _gnu_nonnull(2, 6)
_sppc_api int sppc_pwrite(const int fd, void const *_sppc_restrict buffer, const size_t size, const size_t count,
  const int64_t offset, ssize_t *_sppc_restrict out_n) {
  *out_n = -1;
  _sppc_ofd *const ofd = _sppc_fd_get(fd);
  if (ofd == NULL) { return SPPC_EBADF; }
  int err = 0;
  *out_n = _sppc_ofd_pio(ofd, (void*)buffer, size * count, offset, true, &err);
  _sppc_ofd_release(ofd);
  return err;
}

/// readv and writev: each buffer in turn, stopping at a short one. An error
/// after some bytes moved gives those bytes, as POSIX's partial transfer.
_gnu_inline _gnu_nonnull(2, 4)
int _sppc_win_vector(const int fd, sppc_iovec const *iov, const int iov_count, ssize_t *out_n, const bool write) {
  *out_n = -1;
  if (iov_count < 0 || iov_count > 1024) { return SPPC_EINVAL; }
  _sppc_ofd *const ofd = _sppc_fd_get(fd);
  if (ofd == NULL) { return SPPC_EBADF; }
  ssize_t total = 0;
  int err = 0;
  for (int i = 0; i < iov_count; ++i) {
    const ssize_t moved = write
      ? _sppc_ofd_write(ofd, iov[i].base, iov[i].len, &err)
      : _sppc_ofd_read(ofd, iov[i].base, iov[i].len, &err);
    if (moved < 0) { break; }
    total += moved;
    if ((size_t)moved < iov[i].len) { break; }
  }
  _sppc_ofd_release(ofd);
  if (err != 0 && total == 0) { return err; }
  *out_n = total;
  return 0;
}

_gnu_inline _gnu_restrict_access(read_only, 2) _gnu_restrict_access(write_only, 4) _gnu_nonnull(2, 4)
_sppc_api int sppc_readv(const int fd, sppc_iovec const *_sppc_restrict iov, const int iov_count, ssize_t *_sppc_restrict out_n) {
  return _sppc_win_vector(fd, iov, iov_count, out_n, false);
}

_gnu_inline _gnu_restrict_access(read_only, 2) _gnu_restrict_access(write_only, 4) _gnu_nonnull(2, 4)
_sppc_api int sppc_writev(const int fd, sppc_iovec const *_sppc_restrict iov, const int iov_count,
  ssize_t *_sppc_restrict out_n) {
  return _sppc_win_vector(fd, iov, iov_count, out_n, true);
}

/// To the disk. Not for a pipe or a console (EINVAL, as Linux); a directory,
/// or a file this descriptor cannot write, has nothing of its own to flush.
_gnu_inline
_sppc_api int sppc_fsync(const int fd) {
  _sppc_ofd *const ofd = _sppc_fd_get(fd);
  if (ofd == NULL) { return SPPC_EBADF; }
  int err = 0;
  BY_HANDLE_FILE_INFORMATION info;
  if (!ofd->disk) { err = SPPC_EINVAL; }
  else if ((ofd->access & _SPPC_FD_WRITE) != 0 && GetFileInformationByHandle(ofd->handle, &info)
    && (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 && !FlushFileBuffers(ofd->handle)) {
    err = _sppc_errno_win32(GetLastError());
  }
  _sppc_ofd_release(ofd);
  return err;
}

/// Windows flushes data and metadata together.
_gnu_inline
_sppc_api int sppc_fdatasync(const int fd) {
  return sppc_fsync(fd);
}

_gnu_inline
int _sppc_win_set_length(HANDLE h, const int64_t length) {
  FILE_END_OF_FILE_INFO end;
  end.EndOfFile.QuadPart = length;
  return SetFileInformationByHandle(h, FileEndOfFileInfo, &end, sizeof end) ? 0 : _sppc_errno_win32(GetLastError());
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_nonnull(1)
_sppc_api int sppc_truncate(char const *_sppc_restrict path, const int64_t length) {
  if (length < 0) { return SPPC_EINVAL; }
  wchar_t *w;
  const int bad = _sppc_win_wpath(path, &w);
  if (bad != 0) { return bad; }
  const DWORD attrs = GetFileAttributesW(w);
  int err;
  if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) { err = SPPC_EISDIR; }
  else {
    const HANDLE h = _sppc_win_open(w, GENERIC_WRITE, true);
    if (h == INVALID_HANDLE_VALUE) { err = _sppc_win_path_errno(w, GetLastError()); }
    else {
      err = _sppc_win_set_length(h, length);
      CloseHandle(h);
    }
  }
  free(w);
  return err;
}

/// EBADF for a descriptor not open for writing, EINVAL for one with no
/// length (a pipe, a console), as Linux.
_gnu_inline
_sppc_api int sppc_ftruncate(const int fd, const int64_t length) {
  _sppc_ofd *const ofd = _sppc_fd_get(fd);
  if (ofd == NULL) { return SPPC_EBADF; }
  const int err = length < 0 || !ofd->disk ? SPPC_EINVAL
    : (ofd->access & _SPPC_FD_WRITE) == 0 ? SPPC_EBADF
    : _sppc_win_set_length(ofd->handle, length);
  _sppc_ofd_release(ofd);
  return err;
}

/// Whether this process may use "path" as asked. Windows' own answer is in
/// its ACLs, which sppc does not read: what it reports matches stat's mode.
/// Anyone may read; a read-only file may not be written; only a directory
/// or a program (by its extension) may be run.
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(write_only, 3) _gnu_nonnull(1, 3)
_sppc_api int sppc_access(char const *_sppc_restrict path, const int flags, int *_sppc_restrict out) {
  *out = -1;
  if ((flags & ~(SPPC_R_OK | SPPC_W_OK | SPPC_X_OK)) != 0) { return SPPC_EINVAL; }
  wchar_t *w;
  const int bad = _sppc_win_wpath(path, &w);
  if (bad != 0) { return bad; }
  const DWORD attrs = GetFileAttributesW(w);
  int err = 0;
  if (attrs == INVALID_FILE_ATTRIBUTES) { err = _sppc_win_path_errno(w, GetLastError()); }
  else {
    const bool dir = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    if ((flags & SPPC_W_OK) && !dir && (attrs & FILE_ATTRIBUTE_READONLY)) { err = SPPC_EACCES; }
    if ((flags & SPPC_X_OK) && !dir && !_sppc_win_is_program(w, wcslen(w))) { err = SPPC_EACCES; }
  }
  free(w);
  if (err == 0) { *out = 0; }
  return err;
}

/// Windows' current directory, resolved as POSIX's is (the long names of
/// every directory, the final path to it): ERANGE when it does not fit.
_gnu_inline _gnu_restrict_access(write_only, 1, 2) _gnu_nonnull(1)
_sppc_api int sppc_getcwd(char *_sppc_restrict buffer, const size_t size) {
  if (size == 0) { return SPPC_EINVAL; }
  const HANDLE h = _sppc_win_open(L".", FILE_READ_ATTRIBUTES, true);
  if (h == INVALID_HANDLE_VALUE) { return _sppc_errno_win32(GetLastError()); }
  const DWORD need = GetFinalPathNameByHandleW(h, NULL, 0, FILE_NAME_NORMALIZED);
  wchar_t *const w = need > 0 ? (wchar_t*)malloc(need * sizeof *w) : NULL;
  int err;
  if (w == NULL) { err = need > 0 ? SPPC_ENOMEM : _sppc_errno_win32(GetLastError()); }
  else if (GetFinalPathNameByHandleW(h, w, need, FILE_NAME_NORMALIZED) == 0) { err = _sppc_errno_win32(GetLastError()); }
  else { err = _sppc_win_utf8_path(w, buffer, size, SPPC_ERANGE); }
  free(w);
  CloseHandle(h);
  return err;
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_nonnull(1)
_sppc_api int sppc_chdir(char const *_sppc_restrict path) {
  wchar_t *w;
  const int bad = _sppc_win_wpath(path, &w);
  if (bad != 0) { return bad; }
  const int err = SetCurrentDirectoryW(w) ? 0 : _sppc_win_path_errno(w, GetLastError());
  free(w);
  return err;
}

/// "path" resolved: absolute, every link followed, every "." and ".."
/// gone, long names throughout. "buffer" has room for _SPPC_WIN_PATH_MAX
/// bytes, Windows' MAX_PATH as the stdlib's path_max there (Linux's
/// PATH_MAX is 4096): ENAMETOOLONG for a longer one.
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(write_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_realpath(char const *_sppc_restrict path, char *_sppc_restrict buffer) {
  wchar_t *w;
  const int bad = _sppc_win_wpath(path, &w);
  if (bad != 0) { return bad; }
  const HANDLE h = _sppc_win_open(w, FILE_READ_ATTRIBUTES, true);
  if (h == INVALID_HANDLE_VALUE) {
    const int err = _sppc_win_path_errno(w, GetLastError());
    free(w);
    return err;
  }
  free(w);
  wchar_t final[_SPPC_WIN_PATH_MAX + 8];
  const DWORD len = GetFinalPathNameByHandleW(h, final, sizeof final / sizeof *final, FILE_NAME_NORMALIZED);
  const DWORD win = len == 0 ? GetLastError() : 0;
  CloseHandle(h);
  if (len == 0) { return _sppc_errno_win32(win); }
  if (len >= sizeof final / sizeof *final) { return SPPC_ENAMETOOLONG; }
  return _sppc_win_utf8_path(final, buffer, _SPPC_WIN_PATH_MAX, SPPC_ENAMETOOLONG);
}

// ---------- names ----------

/// Only permission bits, as on POSIX (EINVAL for any other); Windows has
/// nowhere to keep them, so they are not kept.
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_nonnull(1)
_sppc_api int sppc_mkdir(char const *_sppc_restrict path, const uint32_t mode) {
  if ((mode & ~07777u) != 0) { return SPPC_EINVAL; }
  wchar_t *w;
  const int bad = _sppc_win_wpath(path, &w);
  if (bad != 0) { return bad; }
  const int err = CreateDirectoryW(w, NULL) ? 0 : _sppc_win_path_errno(w, GetLastError());
  free(w);
  return err;
}

/// ENOTDIR for anything but a directory, a link to one included, as POSIX's
/// rmdir does not follow one.
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_nonnull(1)
_sppc_api int sppc_rmdir(char const *_sppc_restrict path) {
  wchar_t *w;
  const int bad = _sppc_win_wpath(path, &w);
  if (bad != 0) { return bad; }
  const DWORD attrs = GetFileAttributesW(w);
  int err;
  if (attrs == INVALID_FILE_ATTRIBUTES) { err = _sppc_win_path_errno(w, GetLastError()); }
  else if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0 || (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) { err = SPPC_ENOTDIR; }
  else { err = RemoveDirectoryW(w) ? 0 : _sppc_win_path_errno(w, GetLastError()); }
  free(w);
  return err;
}

// POSIX semantics for a delete: Windows 10 1607 and later, which the SDK
// declares only for a target that new; sppc's is plain Windows 10, so the
// class (its documented value) and its struct are named here. An older
// Windows refuses the class, and the delete falls back.
#define _SPPC_WIN_DISPOSITION_EX ((FILE_INFO_BY_HANDLE_CLASS)21)
#define _SPPC_WIN_DISPOSITION_DELETE 0x1
#define _SPPC_WIN_DISPOSITION_POSIX 0x2
#define _SPPC_WIN_DISPOSITION_IGNORE_READONLY 0x10
typedef struct _sppc_win_disposition_ex {
  DWORD flags;
} _sppc_win_disposition_ex;

/// POSIX's unlink: the name goes at once, even while the file is open (its
/// data lasts until the last handle closes), and a read-only file goes too,
/// as POSIX asks only for the directory's leave. A Windows too old for that
/// (before 10 1809) or a file system without it marks the file for deletion
/// at its last close instead. EISDIR for a directory, as Linux; a symbolic
/// link to one is just a link, and goes.
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_nonnull(1)
_sppc_api int sppc_unlink(char const *_sppc_restrict path) {
  wchar_t *w;
  const int bad = _sppc_win_wpath(path, &w);
  if (bad != 0) { return bad; }
  const DWORD attrs = GetFileAttributesW(w);
  int err = 0;
  if (attrs == INVALID_FILE_ATTRIBUTES) { err = _sppc_win_path_errno(w, GetLastError()); }
  else if ((attrs & FILE_ATTRIBUTE_DIRECTORY) && !(attrs & FILE_ATTRIBUTE_REPARSE_POINT)) { err = SPPC_EISDIR; }
  else {
    const HANDLE h = _sppc_win_open(w, DELETE | FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES, false);
    if (h == INVALID_HANDLE_VALUE) { err = _sppc_win_path_errno(w, GetLastError()); }
    else {
      _sppc_win_disposition_ex posix;
      posix.flags = _SPPC_WIN_DISPOSITION_DELETE | _SPPC_WIN_DISPOSITION_POSIX | _SPPC_WIN_DISPOSITION_IGNORE_READONLY;
      if (!SetFileInformationByHandle(h, _SPPC_WIN_DISPOSITION_EX, &posix, sizeof posix)) {
        if (attrs & FILE_ATTRIBUTE_READONLY) { SetFileAttributesW(w, attrs & ~(DWORD)FILE_ATTRIBUTE_READONLY); }
        FILE_DISPOSITION_INFO old;
        old.DeleteFile = TRUE;
        if (!SetFileInformationByHandle(h, FileDispositionInfo, &old, sizeof old)) { err = _sppc_errno_win32(GetLastError()); }
      }
      CloseHandle(h);
    }
  }
  free(w);
  return err;
}

// POSIX semantics for a rename: Windows 10 1607 and later, named here as the
// delete's are. Replacing, and replacing a file that is open or read-only.
#define _SPPC_WIN_RENAME_EX ((FILE_INFO_BY_HANDLE_CLASS)22)
#define _SPPC_WIN_RENAME_REPLACE 0x1
#define _SPPC_WIN_RENAME_POSIX 0x2
#define _SPPC_WIN_RENAME_IGNORE_READONLY 0x40

/// "from" renamed to "to" with POSIX semantics, false where Windows has none
/// (before 10 1607, or a file system without them, FAT say) with its error
/// in "win". The new name goes to Windows absolute, as it asks.
_gnu_inline _gnu_nonnull(1, 2, 3)
bool _sppc_win_rename_posix(wchar_t const *from, wchar_t const *to, DWORD *win) {
  *win = 0;
  const DWORD full = GetFullPathNameW(to, 0, NULL, NULL);
  const size_t bytes = (size_t)full * sizeof(wchar_t);
  FILE_RENAME_INFO *const info = full > 0 ? (FILE_RENAME_INFO*)calloc(1, sizeof *info + bytes) : NULL;
  if (info == NULL) {
    *win = full > 0 ? ERROR_NOT_ENOUGH_MEMORY : GetLastError();
    return false;
  }
  GetFullPathNameW(to, full, info->FileName, NULL);
  info->FileNameLength = (DWORD)(wcslen(info->FileName) * sizeof(wchar_t));
  // The flags share the first field with the older call's "ReplaceIfExists".
  DWORD flags = _SPPC_WIN_RENAME_REPLACE | _SPPC_WIN_RENAME_POSIX | _SPPC_WIN_RENAME_IGNORE_READONLY;
  memcpy(info, &flags, sizeof flags);
  const HANDLE h = _sppc_win_open(from, DELETE | SYNCHRONIZE, false);
  bool ok = false;
  if (h == INVALID_HANDLE_VALUE) { *win = GetLastError(); }
  else {
    ok = SetFileInformationByHandle(h, _SPPC_WIN_RENAME_EX, info, (DWORD)(sizeof *info + bytes)) != FALSE;
    if (!ok) { *win = GetLastError(); }
    CloseHandle(h);
  }
  free(info);
  return ok;
}

/// POSIX's rename: onto an existing name, which it replaces (a file open
/// elsewhere or read-only too); a directory only onto an empty one
/// (ENOTEMPTY otherwise), a file never onto a directory (EISDIR) nor a
/// directory onto a file (ENOTDIR). EXDEV across volumes. A Windows without
/// POSIX semantics falls back to "MoveFileExW", which cannot replace an open
/// file.
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(read_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_rename(char const *_sppc_restrict old_path, char const *_sppc_restrict new_path) {
  wchar_t *from, *to;
  int err = _sppc_win_wpath(old_path, &from);
  if (err != 0) { return err; }
  err = _sppc_win_wpath(new_path, &to);
  if (err != 0) {
    free(from);
    return err;
  }
  const DWORD src = GetFileAttributesW(from), dst = GetFileAttributesW(to);
  if (src == INVALID_FILE_ATTRIBUTES) { err = _sppc_win_path_errno(from, GetLastError()); }
  else if (dst != INVALID_FILE_ATTRIBUTES) {
    const bool src_dir = (src & FILE_ATTRIBUTE_DIRECTORY) != 0, dst_dir = (dst & FILE_ATTRIBUTE_DIRECTORY) != 0;
    if (dst_dir && !src_dir) { err = SPPC_EISDIR; }
    else if (src_dir && !dst_dir) { err = SPPC_ENOTDIR; }
    else if (dst_dir && !RemoveDirectoryW(to)) {
      const DWORD win = GetLastError();
      err = win == ERROR_DIR_NOT_EMPTY ? SPPC_ENOTEMPTY : _sppc_errno_win32(win);
    }
  }
  DWORD win = 0;
  if (err == 0 && !_sppc_win_rename_posix(from, to, &win)) {
    if (win == ERROR_INVALID_PARAMETER || win == ERROR_NOT_SUPPORTED || win == ERROR_INVALID_FUNCTION) {
      if (dst != INVALID_FILE_ATTRIBUTES && !(dst & FILE_ATTRIBUTE_DIRECTORY) && (dst & FILE_ATTRIBUTE_READONLY)) {
        SetFileAttributesW(to, dst & ~(DWORD)FILE_ATTRIBUTE_READONLY);
      }
      win = MoveFileExW(from, to, MOVEFILE_REPLACE_EXISTING) ? 0 : GetLastError();
    }
    if (win != 0) { err = _sppc_win_path_errno(to, win); }
  }
  free(from);
  free(to);
  return err;
}

/// A second name for a file; EPERM for a directory, as POSIX.
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(read_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_link(char const *_sppc_restrict target, char const *_sppc_restrict linkpath) {
  wchar_t *from, *to;
  int err = _sppc_win_wpath(target, &from);
  if (err != 0) { return err; }
  err = _sppc_win_wpath(linkpath, &to);
  if (err != 0) {
    free(from);
    return err;
  }
  const DWORD attrs = GetFileAttributesW(from);
  if (attrs == INVALID_FILE_ATTRIBUTES) { err = _sppc_win_path_errno(from, GetLastError()); }
  else if (attrs & FILE_ATTRIBUTE_DIRECTORY) { err = SPPC_EPERM; }
  else if (!CreateHardLinkW(to, from, NULL)) { err = _sppc_win_path_errno(to, GetLastError()); }
  free(from);
  free(to);
  return err;
}

#ifndef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
  #define SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE 0x2
#endif

/// A symbolic link to "target", kept as given (relative to the link's own
/// directory, unless absolute) with its separators Windows' own. Windows
/// makes one only for an elevated process or in Developer Mode, EPERM
/// otherwise; and it must know whether the target is a directory, so a
/// target made after the link, of the other kind, does not resolve.
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(read_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_symlink(char const *_sppc_restrict target, char const *_sppc_restrict linkpath) {
  wchar_t *from, *to;
  int err = _sppc_win_wpath(target, &from);
  if (err != 0) { return err; }
  err = _sppc_win_wpath(linkpath, &to);
  if (err != 0) {
    free(from);
    return err;
  }
  for (wchar_t *c = from; *c; ++c) {
    if (*c == L'/') { *c = L'\\'; }
  }

  // Whether the target is a directory, from where the link will be.
  const bool absolute = _sppc_win_is_sep(from[0]) || (from[0] != L'\0' && from[1] == L':');
  DWORD attrs;
  if (absolute) { attrs = GetFileAttributesW(from); }
  else {
    const size_t to_len = wcslen(to), from_len = wcslen(from);
    size_t dir_len = to_len;
    while (dir_len > 0 && !_sppc_win_is_sep(to[dir_len - 1])) { --dir_len; }
    wchar_t *const joined = (wchar_t*)malloc((dir_len + from_len + 1) * sizeof *joined);
    attrs = INVALID_FILE_ATTRIBUTES;
    if (joined != NULL) {
      memcpy(joined, to, dir_len * sizeof *joined);
      memcpy(joined + dir_len, from, (from_len + 1) * sizeof *joined);
      attrs = GetFileAttributesW(joined);
      free(joined);
    }
  }
  const DWORD kind = attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0;
  if (!CreateSymbolicLinkW(to, from, kind | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
    DWORD win = GetLastError();
    // A Windows from before the flag refuses it outright.
    if (win == ERROR_INVALID_PARAMETER && CreateSymbolicLinkW(to, from, kind)) { win = 0; }
    else if (win == ERROR_INVALID_PARAMETER) { win = GetLastError(); }
    if (win != 0) { err = _sppc_win_path_errno(to, win); }
  }
  free(from);
  free(to);
  return err;
}

/// A link's target, terminated: ERANGE (with the buffer emptied) when it and
/// its terminator do not fit, EINVAL for a path that is no symbolic link.
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(write_only, 2, 3) _gnu_nonnull(1, 2)
_sppc_api int sppc_readlink(char const *_sppc_restrict path, char *_sppc_restrict buffer, const size_t buffer_size) {
  if (buffer_size == 0) { return SPPC_ERANGE; }
  wchar_t *w;
  const int bad = _sppc_win_wpath(path, &w);
  if (bad != 0) { return bad; }
  const HANDLE h = _sppc_win_open(w, FILE_READ_ATTRIBUTES, false);
  int err;
  if (h == INVALID_HANDLE_VALUE) { err = _sppc_win_path_errno(w, GetLastError()); }
  else {
    wchar_t *target = NULL;
    err = _sppc_win_link_target(h, &target);
    if (err == 0) {
      const int n = WideCharToMultiByte(CP_UTF8, 0, target, -1, NULL, 0, NULL, NULL);
      if (n <= 0 || (size_t)n > buffer_size) {
        buffer[0] = '\0';
        err = n <= 0 ? SPPC_EILSEQ : SPPC_ERANGE;
      } else {
        WideCharToMultiByte(CP_UTF8, 0, target, -1, buffer, n, NULL, NULL);
      }
      free(target);
    }
    CloseHandle(h);
  }
  free(w);
  return err;
}

// ---------- metadata ----------

/// Only permission bits (EINVAL for any other). Of them Windows keeps one:
/// without the owner's write bit, the file (or directory) is read-only.
/// Follows a symbolic link, as POSIX's chmod.
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_nonnull(1)
_sppc_api int sppc_chmod(char const *_sppc_restrict path, const uint32_t mode) {
  if ((mode & ~07777u) != 0) { return SPPC_EINVAL; }
  wchar_t *w;
  const int bad = _sppc_win_wpath(path, &w);
  if (bad != 0) { return bad; }
  const HANDLE h = _sppc_win_open(w, FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES, true);
  int err = 0;
  if (h == INVALID_HANDLE_VALUE) { err = _sppc_win_path_errno(w, GetLastError()); }
  else {
    FILE_BASIC_INFO basic;
    if (!GetFileInformationByHandleEx(h, FileBasicInfo, &basic, sizeof basic)) { err = _sppc_errno_win32(GetLastError()); }
    else {
      const DWORD was = basic.FileAttributes;
      basic.FileAttributes = (mode & SPPC_S_IWUSR) ? was & ~(DWORD)FILE_ATTRIBUTE_READONLY : was | FILE_ATTRIBUTE_READONLY;
      if (basic.FileAttributes == 0) { basic.FileAttributes = FILE_ATTRIBUTE_NORMAL; }
      // Times of 0 are left as they are.
      basic.CreationTime.QuadPart = basic.LastAccessTime.QuadPart = 0;
      basic.LastWriteTime.QuadPart = basic.ChangeTime.QuadPart = 0;
      if (basic.FileAttributes != was && !SetFileInformationByHandle(h, FileBasicInfo, &basic, sizeof basic)) {
        err = _sppc_errno_win32(GetLastError());
      }
    }
    CloseHandle(h);
  }
  free(w);
  return err;
}

/// Every file belongs to uid and gid 0 (stat), so giving one to them, or
/// leaving either alone (all bits set), is no change, and allowed; anything
/// else needs a privilege no process has here, EPERM, as for an unprivileged
/// POSIX one.
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_nonnull(1)
int _sppc_win_chown(char const *path, const uint32_t owner, const uint32_t group, const bool follow) {
  struct sppc_stat st;
  const int err = _sppc_win_stat_path(path, follow, &st);
  if (err != 0) { return err; }
  return (owner == 0 || owner == UINT32_MAX) && (group == 0 || group == UINT32_MAX) ? 0 : SPPC_EPERM;
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_nonnull(1)
_sppc_api int sppc_chown(char const *_sppc_restrict path, const uint32_t owner, const uint32_t group) {
  return _sppc_win_chown(path, owner, group, true);
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_nonnull(1)
_sppc_api int sppc_lchown(char const *_sppc_restrict path, const uint32_t owner, const uint32_t group) {
  return _sppc_win_chown(path, owner, group, false);
}

/// Its access and modification times, both to now; of a symbolic link
/// itself with AT_SYMLINK_NOFOLLOW.
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_nonnull(1)
_sppc_api int sppc_utimensat(char const *_sppc_restrict path, const int flags) {
  if ((flags & ~SPPC_AT_SYMLINK_NOFOLLOW) != 0) { return SPPC_EINVAL; }
  wchar_t *w;
  const int bad = _sppc_win_wpath(path, &w);
  if (bad != 0) { return bad; }
  const HANDLE h = _sppc_win_open(w, FILE_WRITE_ATTRIBUTES, (flags & SPPC_AT_SYMLINK_NOFOLLOW) == 0);
  int err = 0;
  if (h == INVALID_HANDLE_VALUE) { err = _sppc_win_path_errno(w, GetLastError()); }
  else {
    FILETIME now;
    GetSystemTimePreciseAsFileTime(&now);
    if (!SetFileTime(h, NULL, &now, &now)) { err = _sppc_errno_win32(GetLastError()); }
    CloseHandle(h);
  }
  free(w);
  return err;
}

/// The file system "path" is on: its sizes in blocks of its cluster size,
/// its volume's serial number as its id, the longest name it takes, and
/// whether it is read-only. Windows does not count files, so those are 0.
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(write_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_statvfs(char const *_sppc_restrict path, struct sppc_statvfs *_sppc_restrict out) {
  wchar_t *w;
  const int bad = _sppc_win_wpath(path, &w);
  if (bad != 0) { return bad; }
  wchar_t root[MAX_PATH + 1];
  DWORD sectors = 0, bytes = 0, free_clusters = 0, clusters = 0, serial = 0, longest = 0, fs = 0;
  ULARGE_INTEGER available, total, free_bytes;
  int err = 0;
  if (GetFileAttributesW(w) == INVALID_FILE_ATTRIBUTES) { err = _sppc_win_path_errno(w, GetLastError()); }
  else if (!GetVolumePathNameW(w, root, MAX_PATH + 1) || !GetDiskFreeSpaceW(root, &sectors, &bytes, &free_clusters, &clusters)
    || !GetDiskFreeSpaceExW(root, &available, &total, &free_bytes)
    || !GetVolumeInformationW(root, NULL, 0, &serial, &longest, &fs, NULL, 0)) {
    err = _sppc_errno_win32(GetLastError());
  } else {
    const uint64_t block = (uint64_t)sectors * bytes;
    memset(out, 0, sizeof *out);
    out->block_size = out->frag_size = block;
    out->blocks = total.QuadPart / block;
    out->blocks_free = free_bytes.QuadPart / block;
    out->blocks_available = available.QuadPart / block;
    out->fs_id = serial;
    out->namemax = longest;
    out->flag = (fs & FILE_READ_ONLY_VOLUME) ? SPPC_ST_RDONLY : 0;
  }
  free(w);
  return err;
}

// ---------- temporary names ----------

/// Random bytes from the system's generator.
_gnu_inline _gnu_restrict_access(write_only, 2, 1) _gnu_nonnull(2)
_sppc_api int sppc_getrandom(const size_t size, char *_sppc_restrict out) {
  for (size_t done = 0; done < size;) {
    const ULONG part = size - done > 0x40000000u ? 0x40000000u : (ULONG)(size - done);
    if (!BCRYPT_SUCCESS(BCryptGenRandom(NULL, (PUCHAR)out + done, part, BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
      return SPPC_EIO;
    }
    done += part;
  }
  return 0;
}


/// The template's six X's, made letters and digits at random.
_gnu_inline _gnu_nonnull(1)
void _sppc_win_fill_template(char *path) {
  static char const digits[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  unsigned char random[6];
  sppc_getrandom(sizeof random, (char*)random);
  char *const at = path + strlen(path) - 6;
  for (int i = 0; i < 6; ++i) { at[i] = digits[random[i] % (sizeof digits - 1)]; }
}

/// mkostemp's: a new file of the template's name, made with no one else's
/// by the same name (O_EXCL), read-write and close-on-exec. The template is
/// put back as it was on failure.
_gnu_inline _gnu_restrict_access(read_write, 1) _gnu_restrict_access(write_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_mktemp(char *_sppc_restrict path, int *_sppc_restrict out_fd) {
  *out_fd = -1;
  if (!_sppc_is_temp_template(path)) { return SPPC_EINVAL; }
  int err = SPPC_EEXIST;
  for (int tries = 0; tries < 100 && err == SPPC_EEXIST; ++tries) {
    _sppc_win_fill_template(path);
    err = sppc_open(path, SPPC_O_RDWR | SPPC_O_CREAT | SPPC_O_EXCL | SPPC_O_CLOEXEC, 0600, out_fd);
  }
  if (err != 0) { memcpy(path + strlen(path) - 6, "XXXXXX", 6); }
  return err;
}

/// mkdtemp's: a new directory of the template's name.
_gnu_inline _gnu_restrict_access(read_write, 1) _gnu_nonnull(1)
_sppc_api int sppc_mktemp_dir(char *_sppc_restrict path) {
  if (!_sppc_is_temp_template(path)) { return SPPC_EINVAL; }
  int err = SPPC_EEXIST;
  for (int tries = 0; tries < 100 && err == SPPC_EEXIST; ++tries) {
    _sppc_win_fill_template(path);
    err = sppc_mkdir(path, 0700);
  }
  if (err != 0) { memcpy(path + strlen(path) - 6, "XXXXXX", 6); }
  return err;
}

// ---------- copies ----------

/// "count" bytes from one descriptor to another: from "offset" if it is
/// given (which moves on, the source's own position left alone), else from
/// the source's position. Through a buffer, as Windows has no file-to-file
/// call. Short at the end of the source; an error after some bytes moved
/// gives those bytes. When the destination takes less than was read, the
/// source is put back by the rest, so nothing is skipped.
_gnu_inline _gnu_nonnull(5)
int _sppc_win_copy(const int from_fd, const int to_fd, int64_t *offset, const size_t count, ssize_t *out_n) {
  *out_n = -1;
  _sppc_ofd *const in = _sppc_fd_get(from_fd);
  _sppc_ofd *const out = _sppc_fd_get(to_fd);
  int err = 0;
  ssize_t total = 0;
  char *const buffer = in != NULL && out != NULL ? (char*)malloc(65536) : NULL;
  if (in == NULL || out == NULL) { err = SPPC_EBADF; }
  else if (buffer == NULL) { err = SPPC_ENOMEM; }
  else {
    while ((size_t)total < count) {
      const size_t want = count - (size_t)total < 65536 ? count - (size_t)total : 65536;
      const ssize_t got = offset != NULL
        ? _sppc_ofd_pio(in, buffer, want, *offset + total, false, &err)
        : _sppc_ofd_read(in, buffer, want, &err);
      if (got <= 0) { break; }
      ssize_t put = 0;
      while (put < got) {
        const ssize_t wrote = _sppc_ofd_write(out, buffer + put, (size_t)(got - put), &err);
        if (wrote <= 0) { break; }
        put += wrote;
      }
      total += put;
      if (put < got) {
        if (offset == NULL && in->disk) {
          LARGE_INTEGER back;
          back.QuadPart = -(int64_t)(got - put);
          SetFilePointerEx(in->handle, back, NULL, FILE_CURRENT);
        }
        break;
      }
    }
  }
  free(buffer);
  _sppc_ofd_release(in);
  _sppc_ofd_release(out);
  if (err != 0 && total == 0) { return err; }
  if (offset != NULL) { *offset += total; }
  *out_n = total;
  return 0;
}

_gnu_inline _gnu_restrict_access(write_only, 5) _gnu_nonnull(5)
_sppc_api int sppc_sendfile(const int from_fd, const int to_fd, int64_t *offset, const size_t count,
  ssize_t *_sppc_restrict out_n) {
  return _sppc_win_copy(from_fd, to_fd, offset, count, out_n);
}

/// copy_file_range's: from each descriptor's own position, which it moves
/// on. No flags are defined (EINVAL for any).
_gnu_inline _gnu_restrict_access(write_only, 5) _gnu_nonnull(5)
_sppc_api int sppc_copyfile(const int fd_in, const int fd_out, const size_t len, const int flags,
  ssize_t *_sppc_restrict out_n) {
  if (flags != 0) {
    *out_n = -1;
    return SPPC_EINVAL;
  }
  return _sppc_win_copy(fd_in, fd_out, NULL, len, out_n);
}

// ---------- control ----------

_gnu_inline _gnu_restrict_access(write_only, 3) _gnu_nonnull(3)
_sppc_api int sppc_fcntl_get(const int fd, const int cmd, int *_sppc_restrict out) {
  *out = -1;
  if (cmd != SPPC_F_GETFD && cmd != SPPC_F_GETFL && cmd != SPPC_F_GETOWN) { return SPPC_EINVAL; }
  _sppc_ofd *const ofd = _sppc_fd_get(fd);
  if (ofd == NULL) { return SPPC_EBADF; }
  if (cmd == SPPC_F_GETFD) {
    AcquireSRWLockShared(&_sppc_fds_lock);
    *out = fd < _sppc_fds_cap && _sppc_fds[fd].cloexec ? SPPC_FD_CLOEXEC : 0;
    ReleaseSRWLockShared(&_sppc_fds_lock);
  } else if (cmd == SPPC_F_GETFL) {
    const int access = ofd->access == (_SPPC_FD_READ | _SPPC_FD_WRITE) ? SPPC_O_RDWR
      : ofd->access == _SPPC_FD_WRITE ? SPPC_O_WRONLY : SPPC_O_RDONLY;
    *out = access | (ofd->append ? SPPC_O_APPEND : 0) | (ofd->nonblock ? SPPC_O_NONBLOCK : 0);
  } else {
    *out = 0; // no process owns a descriptor's signals: Windows sends none
  }
  _sppc_ofd_release(ofd);
  return 0;
}

/// F_SETFD: close-on-exec. F_SETFL: O_APPEND and O_NONBLOCK, the status
/// flags a description may change; the rest of an open's flags are taken
/// and left alone, as Linux. F_SETOWN: nothing to own (no signals).
_gnu_inline
_sppc_api int sppc_fcntl_set(const int fd, const int cmd, const int arg) {
  if (cmd == SPPC_F_SETFD && (arg & ~SPPC_FD_CLOEXEC) != 0) { return SPPC_EINVAL; }
  if (cmd == SPPC_F_SETFL && (arg & ~_SPPC_WIN_OPEN_FLAGS) != 0) { return SPPC_EINVAL; }
  if (cmd != SPPC_F_SETFD && cmd != SPPC_F_SETFL && cmd != SPPC_F_SETOWN) { return SPPC_EINVAL; }
  _sppc_ofd *const ofd = _sppc_fd_get(fd);
  if (ofd == NULL) { return SPPC_EBADF; }
  if (cmd == SPPC_F_SETFD) {
    AcquireSRWLockExclusive(&_sppc_fds_lock);
    if (fd < _sppc_fds_cap && _sppc_fds[fd].ofd == ofd) { _sppc_fds[fd].cloexec = (arg & SPPC_FD_CLOEXEC) != 0; }
    ReleaseSRWLockExclusive(&_sppc_fds_lock);
  } else if (cmd == SPPC_F_SETFL) {
    const bool nonblock = (arg & SPPC_O_NONBLOCK) != 0;
    u_long mode = nonblock ? 1 : 0;
    // A socket's blocking is Windows' to keep; nothing else on Windows blocks
    // less for being asked.
    if (ofd->kind == _SPPC_FD_SOCKET && ioctlsocket((SOCKET)(uintptr_t)ofd->handle, FIONBIO, &mode) != 0) {
      const int err = _sppc_errno_wsa(WSAGetLastError());
      _sppc_ofd_release(ofd);
      return err;
    }
    ofd->append = (arg & SPPC_O_APPEND) != 0;
    ofd->nonblock = nonblock;
  }
  _sppc_ofd_release(ofd);
  return 0;
}

/// Record locks on Windows' byte-range locks ("LockFileEx"), which differ
/// from POSIX's in ways that cannot be hidden: they belong to the handle,
/// not the process, so a duplicate shares them but a second open of the
/// file does not; and they are mandatory, so a read or write by another
/// handle into a locked range fails rather than only another lock. A lock
/// over a range this descriptor already locked replaces it, as POSIX's
/// does. A length of 0 runs to the end of any file; a negative one covers
/// the bytes before "start". F_GETLK says only whether the lock would be
/// granted (as F_UNLCK if so); who holds a conflicting one, Windows does not
/// say, so its pid is 0.
_gnu_inline _gnu_nonnull(3)
_sppc_api int sppc_fcntl_lock(const int fd, const int cmd, sppc_flock *_sppc_restrict lock) {
  if ((cmd != SPPC_F_GETLK && cmd != SPPC_F_SETLK && cmd != SPPC_F_SETLKW)
    || (lock->type != SPPC_F_RDLCK && lock->type != SPPC_F_WRLCK && lock->type != SPPC_F_UNLCK)
    || (lock->whence != SPPC_SEEK_SET && lock->whence != SPPC_SEEK_CUR && lock->whence != SPPC_SEEK_END)) {
    return SPPC_EINVAL;
  }
  _sppc_ofd *const ofd = _sppc_fd_get(fd);
  if (ofd == NULL) { return SPPC_EBADF; }
  int err = 0;
  int64_t start = lock->start;
  if (!ofd->disk) { err = SPPC_EINVAL; }
  else if ((lock->type == SPPC_F_RDLCK && !(ofd->access & _SPPC_FD_READ))
    || (lock->type == SPPC_F_WRLCK && !(ofd->access & _SPPC_FD_WRITE))) {
    err = SPPC_EBADF;
  } else if (lock->whence != SPPC_SEEK_SET) {
    LARGE_INTEGER zero, at;
    zero.QuadPart = 0;
    if (lock->whence == SPPC_SEEK_CUR) { SetFilePointerEx(ofd->handle, zero, &at, FILE_CURRENT); }
    else { GetFileSizeEx(ofd->handle, &at); }
    start += at.QuadPart;
  }
  uint64_t length = lock->len == 0 ? UINT64_MAX : (uint64_t)(lock->len < 0 ? -lock->len : lock->len);
  if (lock->len < 0) { start += lock->len; }
  if (err == 0 && start < 0) { err = SPPC_EINVAL; }
  if (err != 0) {
    _sppc_ofd_release(ofd);
    return err;
  }
  if (lock->len == 0) { length -= (uint64_t)start; }

  OVERLAPPED at;
  memset(&at, 0, sizeof at);
  at.Offset = (DWORD)((uint64_t)start & 0xFFFFFFFFu);
  at.OffsetHigh = (DWORD)((uint64_t)start >> 32);
  const DWORD low = (DWORD)(length & 0xFFFFFFFFu), high = (DWORD)(length >> 32);
  const DWORD mode = lock->type == SPPC_F_WRLCK ? LOCKFILE_EXCLUSIVE_LOCK : 0;
  if (cmd == SPPC_F_GETLK) {
    if (lock->type != SPPC_F_UNLCK && LockFileEx(ofd->handle, mode | LOCKFILE_FAIL_IMMEDIATELY, 0, low, high, &at)) {
      UnlockFileEx(ofd->handle, 0, low, high, &at);
      lock->type = SPPC_F_UNLCK;
    } else if (lock->type != SPPC_F_UNLCK) {
      lock->pid = 0;
    }
  } else {
    // Whatever this descriptor held over the range goes first; that it held
    // nothing (ERROR_NOT_LOCKED) is no error.
    UnlockFileEx(ofd->handle, 0, low, high, &at);
    if (lock->type != SPPC_F_UNLCK
      && !LockFileEx(ofd->handle, mode | (cmd == SPPC_F_SETLK ? LOCKFILE_FAIL_IMMEDIATELY : 0), 0, low, high, &at)) {
      const DWORD win = GetLastError();
      err = win == ERROR_LOCK_VIOLATION || win == ERROR_IO_PENDING ? SPPC_EAGAIN : _sppc_errno_win32(win);
    }
  }
  _sppc_ofd_release(ofd);
  return err;
}

// ==================== MEMORY MAPPINGS ====================

// POSIX's one mmap is two things on Windows: private anonymous memory is
// "VirtualAlloc"'s, and the rest (a file, or anonymous memory to share) a
// view of a section ("CreateFileMappingW" + "MapViewOfFile"). "VirtualQuery"
// tells them apart again (MEM_PRIVATE, MEM_MAPPED). sppc lists its views for
// what Windows cannot say: where in a view its mapping starts (a view begins
// at a multiple of 64 KiB into the file, a mapping at any page), and the
// file a shared view writes to, which msync's MS_SYNC flushes.
//
// Where Windows cannot do what POSIX does:
// - a view goes whole: munmap of part of one is EINVAL (private anonymous
//   memory gives back any part, though its address range stays reserved
//   until all of it is unmapped)
// - a file view cannot reach past the page holding the file's end: Windows
//   would grow the file to fit, so such an mmap is refused
// - a file view can execute only if PROT_EXEC was asked for at mmap
// - MAP_NORESERVE means nothing: Windows charges committed memory up front
// - MAP_FIXED is refused: "sppc_mmap" takes no address to fix a mapping at
// - MADV_DONTNEED on a private file view (which would go back to the file's
//   contents) is ENOSYS; on private anonymous memory its pages are
//   decommitted and committed again, empty, so another thread touching them
//   in between faults

/// A view "sppc_mmap" made: where it and the mapping in it start, and, if it
/// is a shared view of a file opened for writing, its own handle of the file.
typedef struct _sppc_win_view {
  void *base;
  char *start;
  HANDLE file; // or NULL
  struct _sppc_win_view *next;
} _sppc_win_view;

// Defined once, in sppc.c.
extern SRWLOCK _sppc_views_lock;
extern _sppc_win_view *_sppc_views;

/// Windows' page protection for sppc's PROT_* bits. A write-only page is
/// readable too, as on every POSIX system on these processors; "copy" is a
/// private view of a file, whose written pages become the process's own.
_gnu_inline
DWORD _sppc_win_page_protect(const int prot, const bool copy) {
  static const DWORD protect[8] = {
    PAGE_NOACCESS, PAGE_READONLY, PAGE_READWRITE, PAGE_READWRITE,
    PAGE_EXECUTE, PAGE_EXECUTE_READ, PAGE_EXECUTE_READWRITE, PAGE_EXECUTE_READWRITE,
  };
  const DWORD p = protect[prot & 7];
  if (copy && p == PAGE_READWRITE) { return PAGE_WRITECOPY; }
  if (copy && p == PAGE_EXECUTE_READWRITE) { return PAGE_EXECUTE_WRITECOPY; }
  return p;
}

/// Whether "mbi" is part of a private view of a file (FILE_MAP_COPY).
_gnu_inline _gnu_nonnull(1)
bool _sppc_win_is_copy_view(MEMORY_BASIC_INFORMATION const *mbi) {
  return mbi->Type == MEM_MAPPED && (mbi->AllocationProtect == PAGE_WRITECOPY
    || mbi->AllocationProtect == PAGE_EXECUTE_WRITECOPY);
}

/// Where the allocation (a VirtualAlloc, or a view) starting at "base" ends.
_gnu_inline
char* _sppc_win_alloc_end(void *base) {
  char *at = (char*)base;
  MEMORY_BASIC_INFORMATION mbi;
  while (VirtualQuery(at, &mbi, sizeof mbi) == sizeof mbi && mbi.AllocationBase == base) {
    at = (char*)mbi.BaseAddress + mbi.RegionSize;
  }
  return at;
}

/// The pages a call on [addr, addr + length) covers, into [*from, *to):
/// EINVAL for an address off a page boundary (unless "round", which rounds
/// it down, as mlock may) or a range past the end of memory.
_gnu_inline _gnu_nonnull(4, 5)
int _sppc_win_page_range(void const *addr, const size_t length, const bool round, char **from, char **to) {
  const uintptr_t page = (uintptr_t)_gt_page();
  uintptr_t start = (uintptr_t)addr;
  if (start % page != 0) {
    if (!round) { return SPPC_EINVAL; }
    start -= start % page;
  }
  const uintptr_t last = (uintptr_t)addr + length;
  if (last < (uintptr_t)addr || last > UINTPTR_MAX - page) { return SPPC_EINVAL; }
  *from = (char*)start;
  *to = (char*)((last + page - 1) / page * page);
  return 0;
}

/// ENOMEM unless every page of [from, to) is mapped, as POSIX reports a
/// range with a hole in it.
_gnu_inline
int _sppc_win_all_mapped(char *from, char *to) {
  MEMORY_BASIC_INFORMATION mbi;
  for (char *at = from; at < to; at = (char*)mbi.BaseAddress + mbi.RegionSize) {
    if (VirtualQuery(at, &mbi, sizeof mbi) != sizeof mbi || mbi.State != MEM_COMMIT) { return SPPC_ENOMEM; }
  }
  return 0;
}

/// Whether any page of [from, to) is locked (VirtualLock); with "unlock",
/// each one that is gets unlocked.
_gnu_inline _gnu_nonnull(4)
int _sppc_win_locked_pages(char *from, char *to, const bool unlock, bool *out_any) {
  const size_t page = _gt_page();
  PSAPI_WORKING_SET_EX_INFORMATION info[64];
  *out_any = false;
  for (char *at = from; at < to;) {
    DWORD n = 0;
    for (; n < 64 && at + (size_t)n * page < to; ++n) { info[n].VirtualAddress = at + (size_t)n * page; }
    if (!QueryWorkingSetEx(GetCurrentProcess(), info, n * (DWORD)sizeof info[0])) {
      return _sppc_errno_win32(GetLastError());
    }
    for (DWORD i = 0; i < n; ++i) {
      if (!info[i].VirtualAttributes.Valid || !info[i].VirtualAttributes.Locked) { continue; }
      *out_any = true;
      if (unlock && !VirtualUnlock(info[i].VirtualAddress, page) && GetLastError() != ERROR_NOT_LOCKED) {
        return _sppc_errno_win32(GetLastError());
      }
    }
    at += (size_t)n * page;
  }
  return 0;
}

/// Put a view on the list; false (and nothing listed) if there is no memory
/// for it. "file" is then the list's, to close.
_gnu_inline
bool _sppc_win_view_add(void *base, char *start, HANDLE file) {
  _sppc_win_view *const view = (_sppc_win_view*)malloc(sizeof *view);
  if (view == NULL) { return false; }
  view->base = base;
  view->start = start;
  view->file = file;
  AcquireSRWLockExclusive(&_sppc_views_lock);
  view->next = _sppc_views;
  _sppc_views = view;
  ReleaseSRWLockExclusive(&_sppc_views_lock);
  return true;
}

/// Where the mapping in the view at "base" starts: the view's own start for
/// one sppc did not make.
_gnu_inline
char* _sppc_win_view_start(void *base) {
  char *start = (char*)base;
  AcquireSRWLockShared(&_sppc_views_lock);
  for (_sppc_win_view *view = _sppc_views; view != NULL; view = view->next) {
    if (view->base == base) {
      start = view->start;
      break;
    }
  }
  ReleaseSRWLockShared(&_sppc_views_lock);
  return start;
}

/// Take the view at "base" off the list, closing its file handle.
_gnu_inline
void _sppc_win_view_forget(void *base) {
  AcquireSRWLockExclusive(&_sppc_views_lock);
  for (_sppc_win_view **at = &_sppc_views; *at != NULL; at = &(*at)->next) {
    if ((*at)->base != base) { continue; }
    _sppc_win_view *const view = *at;
    *at = view->next;
    if (view->file != NULL) { CloseHandle(view->file); }
    free(view);
    break;
  }
  ReleaseSRWLockExclusive(&_sppc_views_lock);
}

/// Flush the file behind the view at "base" to the disk, if it is a shared
/// view of one opened for writing (anything else has nothing to flush).
_gnu_inline
int _sppc_win_view_flush_file(void *base) {
  int err = 0;
  AcquireSRWLockShared(&_sppc_views_lock);
  for (_sppc_win_view *view = _sppc_views; view != NULL; view = view->next) {
    if (view->base == base) {
      if (view->file != NULL && !FlushFileBuffers(view->file)) { err = _sppc_errno_win32(GetLastError()); }
      break;
    }
  }
  ReleaseSRWLockShared(&_sppc_views_lock);
  return err;
}

/// A view of "file" (anonymous memory for INVALID_HANDLE_VALUE, "length"
/// bytes of it), protected as "prot" asks, and listed with "keep" (a handle
/// of the file for MS_SYNC, or NULL; closed if the view is not made). A
/// file's section is the file's own size, so "offset" and "length" must lie
/// in it; the view starts at a multiple of Windows' allocation granularity
/// (64 KiB), and the pointer returned is "offset" into the file.
_gnu_inline
void* _sppc_win_map_view(HANDLE file, const bool shared, const bool writes, const int prot, const uint64_t offset,
  const size_t length, HANDLE keep) {
  SYSTEM_INFO system;
  GetSystemInfo(&system);
  const uint64_t start = offset - offset % system.dwAllocationGranularity;
  const size_t view = (size_t)(offset - start) + length;
  const bool exec = (prot & SPPC_PROT_EXEC) != 0;
  const bool anonymous = file == INVALID_HANDLE_VALUE;

  // The section and the view as much as the descriptor allows, so that a
  // later memprotect can go up to it, as POSIX's may; then the view down to
  // what was asked.
  DWORD section_protect, access;
  if (!shared) {
    section_protect = exec ? PAGE_EXECUTE_WRITECOPY : PAGE_WRITECOPY;
    access = FILE_MAP_COPY;
  } else if (writes) {
    section_protect = exec ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE;
    access = FILE_MAP_WRITE;
  } else {
    section_protect = exec ? PAGE_EXECUTE_READ : PAGE_READONLY;
    access = FILE_MAP_READ;
  }
  if (exec) { access |= FILE_MAP_EXECUTE; }
  const uint64_t size = anonymous ? (uint64_t)length : 0;
  const HANDLE section = CreateFileMappingW(file, NULL, section_protect, (DWORD)(size >> 32), (DWORD)size, NULL);
  char *const base = section == NULL ? NULL : (char*)MapViewOfFile(section, access, (DWORD)(start >> 32), (DWORD)start, view);
  if (section != NULL) { CloseHandle(section); } // the view keeps the section
  DWORD old;
  if (base == NULL || !VirtualProtect(base, view, _sppc_win_page_protect(prot, !shared), &old)
    || !_sppc_win_view_add(base, base + (offset - start), keep)) {
    if (base != NULL) { UnmapViewOfFile(base); }
    if (keep != NULL) { CloseHandle(keep); }
    return NULL;
  }
  return base + (offset - start);
}

/// "sppc_mmap" of a file, "at" bytes into it (a page's multiple).
_gnu_inline
void* _sppc_win_mmap_file(const int fd, const size_t length, const int prot, const bool shared,
  const uint64_t at) {
  const uint64_t page = _gt_page();
  // A file on a disk, open for reading (as POSIX needs of any mapping of
  // it), and for writing too if a shared mapping writes to it.
  _sppc_ofd *const ofd = _sppc_fd_get(fd);
  if (ofd == NULL) { return NULL; }
  const bool writes = shared && (ofd->access & _SPPC_FD_WRITE) != 0;
  LARGE_INTEGER size;
  void *p = NULL;
  const bool mappable = ofd->kind == _SPPC_FD_HANDLE && ofd->disk && (ofd->access & _SPPC_FD_READ) != 0
    && !(shared && (prot & SPPC_PROT_WRITE) && !writes) && GetFileSizeEx(ofd->handle, &size);
  // The file's bytes, and its pages: up to the end of the page holding its
  // end may be mapped, the view rounding up to it with zeroes, as POSIX's.
  const uint64_t bytes = mappable ? (uint64_t)size.QuadPart : 0;
  const uint64_t pages = bytes / page * page + (bytes % page != 0 ? page : 0);
  if (mappable && at < bytes && length <= pages - at) {
    const size_t in_file = bytes - at < length ? (size_t)(bytes - at) : length;
    // Executing needs a handle that may: one of its own, as the descriptor's
    // was opened for reading and writing only.
    HANDLE file = ofd->handle;
    if (prot & SPPC_PROT_EXEC) {
      file = ReOpenFile(ofd->handle, GENERIC_READ | GENERIC_EXECUTE | (writes ? GENERIC_WRITE : 0),
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0);
    }
    // A shared view of a file opened for writing keeps a handle of the file
    // for MS_SYNC (mapped, a POSIX file stays open too): the one made to
    // execute, or one of its own.
    if (file != INVALID_HANDLE_VALUE) {
      HANDLE keep = NULL;
      if (writes && file != ofd->handle) { keep = file; }
      else if (writes) {
        DuplicateHandle(GetCurrentProcess(), file, GetCurrentProcess(), &keep, 0, FALSE, DUPLICATE_SAME_ACCESS);
      }
      if (!writes || keep != NULL) { p = _sppc_win_map_view(file, shared, writes, prot, at, in_file, keep); }
      if (file != ofd->handle && keep != file) { CloseHandle(file); } // the section has the file
    }
  }
  _sppc_ofd_release(ofd);
  return p;
}

/// "sppc_mmap", out of line: inlined into a caller passing one value as
/// both its length and its offset (a page's size, say), msvc 14.52's
/// optimizer at /O2 never finishes, even on the argument checks alone. A
/// mapping is system calls anyway.
_gnu_noinline
inline void* _sppc_win_mmap(const int fd, const size_t length, const int prot, const int flags, const int64_t offset) {
  const int known = SPPC_MAP_SHARED | SPPC_MAP_PRIVATE | SPPC_MAP_ANONYMOUS | SPPC_MAP_NORESERVE;
  const int sharing = flags & (SPPC_MAP_SHARED | SPPC_MAP_PRIVATE);
  const uint64_t page = _gt_page();
  const uint64_t at = (uint64_t)offset;
  if ((prot & ~_SPPC_PROT_ALL) != 0 || (flags & ~known) != 0 || length == 0 || offset < 0
    || (sharing != SPPC_MAP_SHARED && sharing != SPPC_MAP_PRIVATE) || at % page != 0) {
    return NULL;
  }
  const bool shared = sharing == SPPC_MAP_SHARED;

  if (flags & SPPC_MAP_ANONYMOUS) {
    if (!shared) { return VirtualAlloc(NULL, length, MEM_RESERVE | MEM_COMMIT, _sppc_win_page_protect(prot, false)); }
    return _sppc_win_map_view(INVALID_HANDLE_VALUE, true, true, prot, 0, length, NULL);
  }

  return _sppc_win_mmap_file(fd, length, prot, shared, at);
}

_gnu_inline _gnu_malloc _gnu_alloc_size(2)
_sppc_api void* sppc_mmap(const int fd, const size_t length, const int prot, const int flags, const int64_t offset) {
  return _sppc_win_mmap(fd, length, prot, flags, offset);
}

_gnu_inline _gnu_nonnull(1)
_sppc_api int sppc_memprotect(void *addr, const size_t size, const int prot) {
  char *from, *to;
  if ((prot & ~_SPPC_PROT_ALL) != 0) { return SPPC_EINVAL; }
  _return_if_sppc_err(_sppc_win_page_range(addr, size, false, &from, &to))
  _return_if_sppc_err(_sppc_win_all_mapped(from, to))
  // One call per region: VirtualProtect stays inside one allocation, and a
  // private view's pages each go copy-on-write rather than writing through.
  MEMORY_BASIC_INFORMATION mbi;
  for (char *at = from; at < to; at = (char*)mbi.BaseAddress + mbi.RegionSize) {
    VirtualQuery(at, &mbi, sizeof mbi);
    char *const end = (char*)mbi.BaseAddress + mbi.RegionSize < to ? (char*)mbi.BaseAddress + mbi.RegionSize : to;
    DWORD old;
    if (!VirtualProtect(at, (size_t)(end - at), _sppc_win_page_protect(prot, _sppc_win_is_copy_view(&mbi)), &old)) {
      // Writing through a view the file was not opened to write is
      // POSIX's EACCES; so is executing where code may not be made.
      const DWORD win = GetLastError();
      return win == ERROR_INVALID_PARAMETER || win == ERROR_DYNAMIC_CODE_BLOCKED ? SPPC_EACCES : _sppc_errno_win32(win);
    }
  }
  return 0;
}

_gnu_inline _gnu_restrict_access(read_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_munmap(void *_sppc_restrict addr, size_t const *_sppc_restrict length) {
  char *from, *to;
  if (*length == 0) { return SPPC_EINVAL; }
  _return_if_sppc_err(_sppc_win_page_range(addr, *length, false, &from, &to))

  // Looked over first, so that a range Windows cannot unmap leaves it all
  // mapped; then unmapped. Pages not mapped are passed over, as POSIX's are.
  for (int pass = 0; pass < 2; ++pass) {
    MEMORY_BASIC_INFORMATION mbi;
    for (char *at = from; at < to;) {
      if (VirtualQuery(at, &mbi, sizeof mbi) != sizeof mbi) { break; } // past the process's memory
      char *next = (char*)mbi.BaseAddress + mbi.RegionSize;
      char *const base = (char*)mbi.AllocationBase;
      if (mbi.State != MEM_COMMIT) {
        // Nothing there, or a part already unmapped.
      } else if (mbi.Type == MEM_IMAGE) {
        return SPPC_EINVAL; // a loaded module, which only its loader may unmap
      } else if (mbi.Type == MEM_MAPPED) {
        // A view whole: from where the mapping in it starts (a file's
        // offset need only be a page's multiple, a view's 64 KiB's) to its
        // end.
        next = _sppc_win_alloc_end(base);
        if (pass == 0 && (_sppc_win_view_start(base) < from || next > to)) { return SPPC_EINVAL; }
        if (pass == 1) {
          _sppc_win_view_forget(base);
          if (!UnmapViewOfFile(base)) { return _sppc_errno_win32(GetLastError()); }
        }
      } else if (pass == 1) {
        // Private memory: its pages given back, and the address range too
        // once none of it is left.
        char *const end = next < to ? next : to;
        if (!VirtualFree(at, (size_t)(end - at), MEM_DECOMMIT)) { return _sppc_errno_win32(GetLastError()); }
        MEMORY_BASIC_INFORMATION rest;
        bool left = false;
        for (char *r = base; !left && VirtualQuery(r, &rest, sizeof rest) == sizeof rest && rest.AllocationBase == base;
          r = (char*)rest.BaseAddress + rest.RegionSize) {
          left = rest.State == MEM_COMMIT;
        }
        if (!left) { VirtualFree(base, 0, MEM_RELEASE); }
        next = end;
      }
      at = next;
    }
  }
  return 0;
}

_gnu_inline _gnu_restrict_access(read_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_msync(void *_sppc_restrict addr, size_t const *_sppc_restrict length, const int flags) {
  char *from, *to;
  if ((flags & ~(SPPC_MS_ASYNC | SPPC_MS_INVALIDATE | SPPC_MS_SYNC)) != 0
    || (flags & (SPPC_MS_ASYNC | SPPC_MS_SYNC)) == (SPPC_MS_ASYNC | SPPC_MS_SYNC)) {
    return SPPC_EINVAL;
  }
  _return_if_sppc_err(_sppc_win_page_range(addr, *length, false, &from, &to))
  _return_if_sppc_err(_sppc_win_all_mapped(from, to))
  // A view's pages go to the file (MS_ASYNC: started, as POSIX's may be);
  // MS_SYNC then waits for the file to reach the disk. Private memory has
  // no file, and a view's pages are the file's already (MS_INVALIDATE).
  MEMORY_BASIC_INFORMATION mbi;
  for (char *at = from; at < to; at = (char*)mbi.BaseAddress + mbi.RegionSize) {
    VirtualQuery(at, &mbi, sizeof mbi);
    if (mbi.Type != MEM_MAPPED || _sppc_win_is_copy_view(&mbi)) { continue; }
    char *const end = (char*)mbi.BaseAddress + mbi.RegionSize < to ? (char*)mbi.BaseAddress + mbi.RegionSize : to;
    if (!FlushViewOfFile(at, (size_t)(end - at))) { return _sppc_errno_win32(GetLastError()); }
    if (flags & SPPC_MS_SYNC) { _return_if_sppc_err(_sppc_win_view_flush_file(mbi.AllocationBase)) }
  }
  return 0;
}

_gnu_inline _gnu_restrict_access(read_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_madvise(void *_sppc_restrict addr, size_t const *_sppc_restrict length, const int advice) {
  char *from, *to;
  if (advice != SPPC_MADV_NORMAL && advice != SPPC_MADV_RANDOM && advice != SPPC_MADV_SEQUENTIAL
    && advice != SPPC_MADV_WILLNEED && advice != SPPC_MADV_DONTNEED && advice != SPPC_MADV_FREE) {
    return SPPC_EINVAL;
  }
  _return_if_sppc_err(_sppc_win_page_range(addr, *length, false, &from, &to))
  _return_if_sppc_err(_sppc_win_all_mapped(from, to))
  if (from == to) { return 0; }

  if (advice == SPPC_MADV_WILLNEED) {
    // Read ahead; a hint, so a refusal is no error.
    WIN32_MEMORY_RANGE_ENTRY range = {from, (SIZE_T)(to - from)};
    PrefetchVirtualMemory(GetCurrentProcess(), 1, &range, 0);
    return 0;
  }
  if (advice != SPPC_MADV_DONTNEED && advice != SPPC_MADV_FREE) { return 0; } // hints Windows takes none of

  // As Linux's: locked pages are not given up, and MADV_FREE is for private
  // anonymous memory only.
  bool locked;
  _return_if_sppc_err(_sppc_win_locked_pages(from, to, false, &locked))
  if (locked) { return SPPC_EINVAL; }
  MEMORY_BASIC_INFORMATION mbi;
  for (char *at = from; at < to; at = (char*)mbi.BaseAddress + mbi.RegionSize) {
    VirtualQuery(at, &mbi, sizeof mbi);
    if (mbi.Type != MEM_PRIVATE && advice == SPPC_MADV_FREE) { return SPPC_EINVAL; }
    if (_sppc_win_is_copy_view(&mbi)) { return SPPC_ENOSYS; }
  }

  for (char *at = from; at < to; at = (char*)mbi.BaseAddress + mbi.RegionSize) {
    VirtualQuery(at, &mbi, sizeof mbi);
    char *const end = (char*)mbi.BaseAddress + mbi.RegionSize < to ? (char*)mbi.BaseAddress + mbi.RegionSize : to;
    const size_t n = (size_t)(end - at);
    if (mbi.Type == MEM_MAPPED) {
      continue; // shared: the pages are the file's or the section's, and keep what is in them
    }
    if (advice == SPPC_MADV_FREE) {
      // May be thrown away, or not, until next written: Windows' discard.
      if (DiscardVirtualMemory(at, n) != ERROR_SUCCESS) { VirtualAlloc(at, n, MEM_RESET, PAGE_NOACCESS); }
      continue;
    }
    // Linux's MADV_DONTNEED empties private memory: given back, and taken
    // again as fresh zeroed pages with the protection they had.
    if (!VirtualFree(at, n, MEM_DECOMMIT) || VirtualAlloc(at, n, MEM_COMMIT, mbi.Protect) == NULL) {
      return _sppc_errno_win32(GetLastError());
    }
  }
  return 0;
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_nonnull(1)
_sppc_api int sppc_mlock(const void *addr, const size_t size) {
  char *from, *to;
  _return_if_sppc_err(_sppc_win_page_range(addr, size, true, &from, &to))
  if (from == to) { return 0; }
  _return_if_sppc_err(_sppc_win_all_mapped(from, to))
  if (VirtualLock(from, (size_t)(to - from))) { return 0; }
  // Windows locks no more than the working set's minimum allows (some
  // hundreds of KiB by default, where Linux allows 8 MiB): raise it by what
  // is asked, and try again.
  SIZE_T low, high;
  if (GetLastError() == ERROR_WORKING_SET_QUOTA && GetProcessWorkingSetSize(GetCurrentProcess(), &low, &high)) {
    const SIZE_T more = (SIZE_T)(to - from);
    if (SetProcessWorkingSetSize(GetCurrentProcess(), low + more, high > low + more ? high : low + more)
      && VirtualLock(from, more)) {
      return 0;
    }
  }
  return _sppc_errno_win32(GetLastError());
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_nonnull(1)
_sppc_api int sppc_munlock(const void *addr, const size_t size) {
  char *from, *to;
  _return_if_sppc_err(_sppc_win_page_range(addr, size, true, &from, &to))
  _return_if_sppc_err(_sppc_win_all_mapped(from, to))
  // Page by page, only those locked: VirtualUnlock of a page that is not
  // fails, where POSIX's munlock does not mind.
  bool any;
  return _sppc_win_locked_pages(from, to, true, &any);
}

// ==================== SOCKETS ====================

// Winsock's sockets, as descriptors of their own kind (FD TABLE): read and
// write work on them as on POSIX. Addresses are the platform's own, made and
// read by sppc's helpers (platform/common.h); the tables of families, types,
// flags and options are every platform's (platform/shared.h). Where Winsock
// differs from POSIX's sockets it is made not to: MSG_DONTWAIT asks whether
// the socket is ready first; a datagram too big for the buffer gives its start;
// an earlier "port unreachable" does not fail a later receive; SO_REUSEADDR is
// kept, not given to Windows, whose own lets another process bind the same
// port; receive and send timeouts are milliseconds there. Every socket is
// overlapped-capable, for the IOCP backend to come.

#ifndef WSA_FLAG_NO_HANDLE_INHERIT
  #define WSA_FLAG_NO_HANDLE_INHERIT 0x80
#endif
#ifndef SIO_UDP_CONNRESET
  #define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

_gnu_inline
int _sppc_win_wsa_errno(void) {
  return _sppc_errno_wsa(WSAGetLastError());
}

/// A send's or a receive's error on a connection Windows has dropped: it
/// says WSAECONNABORTED, where POSIX says EPIPE to a sender (as for a pipe
/// with no reader) and ECONNRESET to a receiver. Anything else as for any
/// call.
_gnu_inline
int _sppc_win_transfer_errno(const int win, const bool sending) {
  if (win == WSAECONNABORTED) { return sending ? SPPC_EPIPE : SPPC_ECONNRESET; }
  return _sppc_errno_wsa(win);
}

/// The socket "fd" names, its description held for the caller to release:
/// EBADF for no descriptor, ENOTSOCK for one of anything else.
_gnu_inline _gnu_nonnull(2, 3)
int _sppc_win_socket(const int fd, _sppc_ofd **ofd, SOCKET *out) {
  *ofd = _sppc_fd_get(fd);
  if (*ofd == NULL) { return SPPC_EBADF; }
  if ((*ofd)->kind != _SPPC_FD_SOCKET) {
    _sppc_ofd_release(*ofd);
    *ofd = NULL;
    return SPPC_ENOTSOCK;
  }
  *out = (SOCKET)(uintptr_t)(*ofd)->handle;
  return 0;
}

/// A descriptor for a new socket, which then owns it (closed again if it
/// cannot have one).
_gnu_inline _gnu_nonnull(4)
int _sppc_win_socket_fd(const SOCKET socket, const bool nonblock, const bool cloexec, int *out_fd) {
  *out_fd = -1;
  _sppc_ofd *const ofd = (_sppc_ofd*)calloc(1, sizeof *ofd);
  if (ofd == NULL) {
    closesocket(socket);
    return SPPC_ENOMEM;
  }
  ofd->refs = 1;
  ofd->kind = _SPPC_FD_SOCKET;
  ofd->access = _SPPC_FD_READ | _SPPC_FD_WRITE;
  ofd->handle = (HANDLE)(uintptr_t)socket;
  ofd->nonblock = nonblock;
  InitializeSRWLock(&ofd->lock);
  return _sppc_fd_install(ofd, 0, cloexec, out_fd);
}

_gnu_inline _gnu_restrict_access(write_only, 4) _gnu_nonnull(4)
_sppc_api int sppc_socket(const int domain, const int type, const int protocol, int *_sppc_restrict out_fd) {
  *out_fd = -1;
  size_t n;
  _sppc_pair const *const families = _sppc_af_table(&n);
  int os_domain, os_type;
  if (_sppc_to_os_value(domain, families, n, &os_domain) != 0 || _sppc_to_os_sock_type(type, &os_type) != 0) {
    return SPPC_EINVAL;
  }
  const bool nonblock = (os_type & _SPPC_OS_SOCK_NONBLOCK) != 0, cloexec = (os_type & _SPPC_OS_SOCK_CLOEXEC) != 0;
  os_type &= ~(_SPPC_OS_SOCK_NONBLOCK | _SPPC_OS_SOCK_CLOEXEC);
  const SOCKET socket = WSASocketW(os_domain, os_type, protocol, NULL, 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
  if (socket == INVALID_SOCKET) { return _sppc_win_wsa_errno(); }
  u_long on = 1;
  if (nonblock && ioctlsocket(socket, FIONBIO, &on) != 0) {
    const int err = _sppc_win_wsa_errno();
    closesocket(socket);
    return err;
  }
  if (os_type == SOCK_DGRAM) {
    BOOL report = FALSE;
    DWORD unused = 0;
    WSAIoctl(socket, SIO_UDP_CONNRESET, &report, sizeof report, NULL, 0, &unused, NULL, NULL);
  }
  return _sppc_win_socket_fd(socket, nonblock, cloexec, out_fd);
}

/// A Winsock extension's function (AcceptEx, ConnectEx) for "socket"'s
/// provider, into "fn" (a function pointer's size); false if it has none.
_gnu_inline _gnu_nonnull(2, 3)
bool _sppc_win_extension(const SOCKET socket, GUID const *id, void *fn, const DWORD size) {
  DWORD got = 0;
  return WSAIoctl(socket, SIO_GET_EXTENSION_FUNCTION_POINTER, (void*)(uintptr_t)id, sizeof *id, fn, size, &got, NULL, NULL)
    == 0;
}

/// "connect" through the completion port, with ConnectEx (a blocking TCP
/// socket of IPv4 or IPv6, on a thread with a port): Winsock's error, or 0;
/// "*went" false where it could not, for the plain call to make instead.
/// ConnectEx needs a bound socket, so one not bound yet is bound as connect
/// would bind it, to any address and a port of the system's choosing.
_gnu_inline _gnu_nonnull(1, 2, 4)
int _sppc_win_connect_on_port(_sppc_ofd *ofd, struct sockaddr const *addr, const int len, bool *went) {
  *went = false;
#if SPPC_ASYNC_IOCP
  const SOCKET socket = (SOCKET)(uintptr_t)ofd->handle;
  int type = 0, type_len = (int)sizeof type;
  LPFN_CONNECTEX connect_ex = NULL;
  const GUID id = WSAID_CONNECTEX;
  if (ofd->nonblock || !_gt_io_ready() || (addr->sa_family != AF_INET && addr->sa_family != AF_INET6)
    || getsockopt(socket, SOL_SOCKET, SO_TYPE, (char*)&type, &type_len) != 0 || type != SOCK_STREAM
    || !_sppc_win_extension(socket, &id, &connect_ex, sizeof connect_ex)) {
    return 0;
  }
  struct sockaddr_storage here;
  int here_len = (int)sizeof here;
  if (getsockname(socket, (struct sockaddr*)&here, &here_len) != 0) {
    if (WSAGetLastError() != WSAEINVAL) { return 0; }
    memset(&here, 0, sizeof here);
    here.ss_family = addr->sa_family;
    here_len = addr->sa_family == AF_INET6 ? (int)sizeof(struct sockaddr_in6) : (int)sizeof(struct sockaddr_in);
    if (bind(socket, (struct sockaddr*)&here, here_len) != 0) { return 0; }
  }
  if (!_sppc_win_on_port(ofd)) { return 0; }
  *went = true;
  OVERLAPPED *const ov = _gt_iocp_overlapped();
  if (!connect_ex(socket, addr, len, NULL, 0, NULL, ov) && WSAGetLastError() != ERROR_IO_PENDING) { return WSAGetLastError(); }
  _gt_iocp_wait(ofd->handle);
  DWORD done = 0, how = 0;
  if (!WSAGetOverlappedResult(socket, ov, &done, FALSE, &how)) { return WSAGetLastError(); }
  // What a plain connect leaves set: getpeername, shutdown and the rest.
  setsockopt(socket, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0);
  return 0;
#else
  (void)ofd;
  (void)addr;
  (void)len;
  return 0;
#endif
}

/// "accept" through the completion port, with AcceptEx (a blocking
/// listener, on a thread with a port): the connection with its peer's
/// address in "addr" (and "len"), or INVALID_SOCKET with Winsock's error in
/// "win"; "*went" false where it could not, for the plain call instead.
_gnu_inline _gnu_nonnull(1, 2, 3, 4, 5)
SOCKET _sppc_win_accept_on_port(_sppc_ofd *ofd, struct sockaddr *addr, int *len, int *win, bool *went) {
  *went = false;
  *win = 0;
#if SPPC_ASYNC_IOCP
  const SOCKET listener = (SOCKET)(uintptr_t)ofd->handle;
  LPFN_ACCEPTEX accept_ex = NULL;
  const GUID id = WSAID_ACCEPTEX;
  WSAPROTOCOL_INFOW info;
  int info_len = (int)sizeof info;
  if (ofd->nonblock || !_gt_io_ready() || !_sppc_win_extension(listener, &id, &accept_ex, sizeof accept_ex)
    || getsockopt(listener, SOL_SOCKET, SO_PROTOCOL_INFOW, (char*)&info, &info_len) != 0 || !_sppc_win_on_port(ofd)) {
    return INVALID_SOCKET;
  }
  *went = true;
  // AcceptEx takes the socket the connection becomes, made as "sppc_socket"
  // makes one, and room for both ends' addresses (16 bytes over each).
  const SOCKET conn = WSASocketW(info.iAddressFamily, info.iSocketType, info.iProtocol, NULL, 0,
    WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
  if (conn == INVALID_SOCKET) {
    *win = WSAGetLastError();
    return INVALID_SOCKET;
  }
  char addrs[2 * (sizeof(struct sockaddr_storage) + 16)];
  const DWORD each = (DWORD)sizeof(struct sockaddr_storage) + 16;
  DWORD done = 0, how = 0;
  OVERLAPPED *const ov = _gt_iocp_overlapped();
  if (!accept_ex(listener, conn, addrs, 0, each, each, &done, ov) && WSAGetLastError() != ERROR_IO_PENDING) {
    *win = WSAGetLastError();
    closesocket(conn);
    return INVALID_SOCKET;
  }
  _gt_iocp_wait(ofd->handle);
  if (!WSAGetOverlappedResult(listener, ov, &done, FALSE, &how)) {
    *win = WSAGetLastError();
    closesocket(conn);
    return INVALID_SOCKET;
  }
  // What a plain accept leaves set: the listener's options, getpeername.
  setsockopt(conn, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char const*)&listener, (int)sizeof listener);
  if (getpeername(conn, addr, len) != 0) { memset(addr, 0, (size_t)*len); }
  return conn;
#else
  (void)ofd;
  (void)addr;
  (void)len;
  return INVALID_SOCKET;
#endif
}

/// EINPROGRESS for a non-blocking socket still connecting, as POSIX; EALREADY
/// for a connect while one is.
_gnu_inline _gnu_restrict_access(read_only, 2) _gnu_nonnull(2)
_sppc_api int sppc_connect(const int fd, sppc_sockaddr_storage const *_sppc_restrict storage) {
  _sppc_ofd *ofd;
  SOCKET socket;
  int err = _sppc_win_socket(fd, &ofd, &socket);
  if (err != 0) { return err; }
  struct sockaddr_storage const *const ss = _sppc_os_sockaddr_const(storage);
  bool went;
  const int on_port = _sppc_win_connect_on_port(ofd, (struct sockaddr const*)ss, (int)_sppc_sockaddr_len(ss), &went);
  if (went) {
    if (on_port != 0) { err = _sppc_errno_wsa(on_port); }
  } else if (connect(socket, (struct sockaddr const*)ss, (int)_sppc_sockaddr_len(ss)) == SOCKET_ERROR) {
    const int win = WSAGetLastError();
    err = win == WSAEWOULDBLOCK ? SPPC_EINPROGRESS
      : win == WSAEINVAL && ofd->nonblock ? SPPC_EALREADY
      : _sppc_errno_wsa(win);
  }
  _sppc_ofd_release(ofd);
  return err;
}

_gnu_inline _gnu_restrict_access(read_only, 2) _gnu_nonnull(2)
_sppc_api int sppc_bind(const int fd, sppc_sockaddr_storage const *_sppc_restrict storage) {
  _sppc_ofd *ofd;
  SOCKET socket;
  int err = _sppc_win_socket(fd, &ofd, &socket);
  if (err != 0) { return err; }
  struct sockaddr_storage const *const ss = _sppc_os_sockaddr_const(storage);
  if (bind(socket, (struct sockaddr const*)ss, (int)_sppc_sockaddr_len(ss)) == SOCKET_ERROR) { err = _sppc_win_wsa_errno(); }
  _sppc_ofd_release(ofd);
  return err;
}

_gnu_inline
_sppc_api int sppc_listen(const int fd, const int backlog) {
  _sppc_ofd *ofd;
  SOCKET socket;
  int err = _sppc_win_socket(fd, &ofd, &socket);
  if (err != 0) { return err; }
  if (listen(socket, backlog) == SOCKET_ERROR) { err = _sppc_win_wsa_errno(); }
  _sppc_ofd_release(ofd);
  return err;
}

/// The connection's descriptor is close-on-exec, as accept4's with
/// SOCK_CLOEXEC, and blocks, as POSIX's does: Windows' would take the
/// listener's non-blocking mode.
_gnu_inline _gnu_restrict_access(write_only, 2) _gnu_restrict_access(write_only, 3) _gnu_nonnull(2, 3)
_sppc_api int sppc_accept(const int fd, sppc_sockaddr_storage *_sppc_restrict out_storage, int *_sppc_restrict out_fd) {
  *out_fd = -1;
  _sppc_ofd *ofd;
  SOCKET socket;
  const int err = _sppc_win_socket(fd, &ofd, &socket);
  if (err != 0) { return err; }
  int len = (int)sizeof(struct sockaddr_storage), win;
  bool went;
  SOCKET conn = _sppc_win_accept_on_port(ofd, (struct sockaddr*)_sppc_os_sockaddr(out_storage), &len, &win, &went);
  if (!went) {
    conn = accept(socket, (struct sockaddr*)_sppc_os_sockaddr(out_storage), &len);
    win = conn == INVALID_SOCKET ? WSAGetLastError() : 0;
  }
  _sppc_ofd_release(ofd);
  if (conn == INVALID_SOCKET) { return _sppc_errno_wsa(win); }
  u_long off = 0;
  ioctlsocket(conn, FIONBIO, &off);
  SetHandleInformation((HANDLE)(uintptr_t)conn, HANDLE_FLAG_INHERIT, 0);
  return _sppc_win_socket_fd(conn, false, true, out_fd);
}

_gnu_inline _gnu_restrict_access(write_only, 2) _gnu_nonnull(2)
_sppc_api int sppc_getsockname(const int fd, sppc_sockaddr_storage *_sppc_restrict out_storage) {
  _sppc_ofd *ofd;
  SOCKET socket;
  int err = _sppc_win_socket(fd, &ofd, &socket);
  if (err != 0) { return err; }
  int len = (int)sizeof(struct sockaddr_storage);
  if (getsockname(socket, (struct sockaddr*)_sppc_os_sockaddr(out_storage), &len) == SOCKET_ERROR) { err = _sppc_win_wsa_errno(); }
  _sppc_ofd_release(ofd);
  return err;
}

_gnu_inline _gnu_restrict_access(write_only, 2) _gnu_nonnull(2)
_sppc_api int sppc_getpeername(const int fd, sppc_sockaddr_storage *_sppc_restrict out_storage) {
  _sppc_ofd *ofd;
  SOCKET socket;
  int err = _sppc_win_socket(fd, &ofd, &socket);
  if (err != 0) { return err; }
  int len = (int)sizeof(struct sockaddr_storage);
  if (getpeername(socket, (struct sockaddr*)_sppc_os_sockaddr(out_storage), &len) == SOCKET_ERROR) { err = _sppc_win_wsa_errno(); }
  _sppc_ofd_release(ofd);
  return err;
}

/// SD_RECEIVE, SD_SEND and SD_BOTH are SHUT_RD, SHUT_WR and SHUT_RDWR.
_gnu_inline
_sppc_api int sppc_shutdown(const int fd, const int how) {
  if (how != SPPC_SHUT_RD && how != SPPC_SHUT_WR && how != SPPC_SHUT_RDWR) { return SPPC_EINVAL; }
  _sppc_ofd *ofd;
  SOCKET socket;
  int err = _sppc_win_socket(fd, &ofd, &socket);
  if (err != 0) { return err; }
  if (shutdown(socket, how) == SOCKET_ERROR) { err = _sppc_win_wsa_errno(); }
  _sppc_ofd_release(ofd);
  return err;
}

/// MSG_NOSIGNAL is what Windows always does; MSG_DONTWAIT it has no flag
/// for, so it asks first whether the socket would take or give anything
/// now (EAGAIN if not), which a blocking socket then does without waiting.
/// The rest are the platform's (platform/shared.h). EINVAL for any other.
_gnu_inline _gnu_nonnull(3, 4)
int _sppc_win_msg_flags(const int flags, const SOCKET socket, const bool write, int *os_flags) {
  if (_sppc_to_os_msg_flags(flags & ~(SPPC_MSG_NOSIGNAL | SPPC_MSG_DONTWAIT), os_flags) != 0) { return SPPC_EINVAL; }
  if ((flags & SPPC_MSG_DONTWAIT) == 0) { return 0; }
  WSAPOLLFD ready;
  ready.fd = socket;
  ready.events = write ? POLLWRNORM : POLLRDNORM;
  ready.revents = 0;
  const int n = WSAPoll(&ready, 1, 0);
  if (n == SOCKET_ERROR) { return _sppc_win_wsa_errno(); }
  return n == 0 ? SPPC_EAGAIN : 0;
}

_gnu_inline _gnu_restrict_access(read_only, 2, 3) _gnu_restrict_access(write_only, 5) _gnu_nonnull(2, 5)
_sppc_api int sppc_send(const int fd, char const *data, const size_t size, const int flags, ssize_t *_sppc_restrict out_n) {
  *out_n = -1;
  _sppc_ofd *ofd;
  SOCKET socket;
  int err = _sppc_win_socket(fd, &ofd, &socket);
  if (err != 0) { return err; }
  int os_flags = 0;
  err = _sppc_win_msg_flags(flags, socket, true, &os_flags);
  if (err == 0) {
    int win;
    const int sent = _sppc_win_transfer(ofd, (char*)(uintptr_t)data, size < INT_MAX ? (int)size : INT_MAX, os_flags, true,
      NULL, NULL, &win, (flags & SPPC_MSG_DONTWAIT) == 0);
    if (sent == SOCKET_ERROR) { err = _sppc_win_transfer_errno(win, true); }
    else { *out_n = sent; }
  }
  _sppc_ofd_release(ofd);
  return err;
}

/// A datagram longer than the buffer gives its start, the rest lost, as on
/// POSIX: Windows calls that an error (WSAEMSGSIZE) having filled the buffer.
_gnu_inline _gnu_restrict_access(write_only, 2, 3) _gnu_restrict_access(write_only, 5) _gnu_nonnull(2, 5)
_sppc_api int sppc_recv(const int fd, char *buffer, const size_t size, const int flags, ssize_t *_sppc_restrict out_n) {
  *out_n = -1;
  _sppc_ofd *ofd;
  SOCKET socket;
  int err = _sppc_win_socket(fd, &ofd, &socket);
  if (err != 0) { return err; }
  int os_flags = 0;
  err = _sppc_win_msg_flags(flags, socket, false, &os_flags);
  if (err == 0) {
    const int want = size < INT_MAX ? (int)size : INT_MAX;
    int win;
    const int got = _sppc_win_transfer(ofd, buffer, want, os_flags, false, NULL, NULL, &win, (flags & SPPC_MSG_DONTWAIT) == 0);
    if (got != SOCKET_ERROR) { *out_n = got; }
    else if (win == WSAEMSGSIZE) { *out_n = want; }
    else { err = _sppc_win_transfer_errno(win, false); }
  }
  _sppc_ofd_release(ofd);
  return err;
}

_gnu_inline _gnu_restrict_access(read_only, 2, 3) _gnu_restrict_access(read_only, 4) _gnu_restrict_access(write_only, 5)
_gnu_nonnull(2, 4, 5)
_sppc_api int sppc_sendto(const int fd, char const *data, const size_t size,
  sppc_sockaddr_storage const *_sppc_restrict storage, ssize_t *_sppc_restrict out_n) {
  *out_n = -1;
  _sppc_ofd *ofd;
  SOCKET socket;
  int err = _sppc_win_socket(fd, &ofd, &socket);
  if (err != 0) { return err; }
  struct sockaddr_storage const *const ss = _sppc_os_sockaddr_const(storage);
  int len = (int)_sppc_sockaddr_len(ss), win;
  const int sent = _sppc_win_transfer(ofd, (char*)(uintptr_t)data, size < INT_MAX ? (int)size : INT_MAX, 0, true,
    (struct sockaddr*)(uintptr_t)ss, &len, &win, true);
  if (sent == SOCKET_ERROR) { err = _sppc_win_transfer_errno(win, true); }
  else { *out_n = sent; }
  _sppc_ofd_release(ofd);
  return err;
}

_gnu_inline _gnu_restrict_access(write_only, 2, 3) _gnu_restrict_access(write_only, 4) _gnu_restrict_access(write_only, 5)
_gnu_nonnull(2, 4, 5)
_sppc_api int sppc_recvfrom(const int fd, char *buffer, const size_t size,
  sppc_sockaddr_storage *_sppc_restrict out_storage, ssize_t *_sppc_restrict out_n) {
  *out_n = -1;
  _sppc_ofd *ofd;
  SOCKET socket;
  int err = _sppc_win_socket(fd, &ofd, &socket);
  if (err != 0) { return err; }
  int len = (int)sizeof(struct sockaddr_storage), win;
  const int want = size < INT_MAX ? (int)size : INT_MAX;
  const int got = _sppc_win_transfer(ofd, buffer, want, 0, false, (struct sockaddr*)_sppc_os_sockaddr(out_storage), &len,
    &win, true);
  if (got != SOCKET_ERROR) { *out_n = got; }
  else if (win == WSAEMSGSIZE) { *out_n = want; }
  else { err = _sppc_win_transfer_errno(win, false); }
  _sppc_ofd_release(ofd);
  return err;
}

// ---------- options ----------

/// The option for "level" and "name", on the socket "fd" names: its
/// description held for the caller to release.
_gnu_inline _gnu_nonnull(4, 5, 6, 7)
int _sppc_win_sockopt(const int fd, const int level, const int name, _sppc_ofd **ofd, SOCKET *socket, int *os_level,
  int *os_name) {
  const int bad = _sppc_to_os_sockopt(level, name, os_level, os_name);
  return bad != 0 ? bad : _sppc_win_socket(fd, ofd, socket);
}

_gnu_inline _gnu_restrict_access(read_only, 4) _gnu_nonnull(4)
_sppc_api int sppc_setsockopt(const int fd, const int level, const int optname, int const *_sppc_restrict optval) {
  _sppc_ofd *ofd;
  SOCKET socket;
  int os_level, os_name;
  int err = _sppc_win_sockopt(fd, level, optname, &ofd, &socket, &os_level, &os_name);
  if (err != 0) { return err; }
  if (level == SPPC_SOL_SOCKET && optname == SPPC_SO_REUSEADDR) { ofd->reuseaddr = *optval != 0; }
  else if (setsockopt(socket, os_level, os_name, (char const*)optval, (int)sizeof *optval) == SOCKET_ERROR) {
    err = _sppc_win_wsa_errno();
  }
  _sppc_ofd_release(ofd);
  return err;
}

/// SO_ERROR comes back as an sppc errno and SO_TYPE as an sppc type, as on
/// POSIX. A Boolean option Windows answers in fewer bytes than an int (a
/// BOOLEAN, for TCP_NODELAY) lands in a zeroed one.
_gnu_inline _gnu_restrict_access(write_only, 4) _gnu_nonnull(4)
_sppc_api int sppc_getsockopt(const int fd, const int level, const int optname, int *_sppc_restrict optval) {
  _sppc_ofd *ofd;
  SOCKET socket;
  int os_level, os_name;
  int err = _sppc_win_sockopt(fd, level, optname, &ofd, &socket, &os_level, &os_name);
  if (err != 0) { return err; }
  *optval = 0;
  int len = (int)sizeof *optval;
  if (level == SPPC_SOL_SOCKET && optname == SPPC_SO_REUSEADDR) { *optval = ofd->reuseaddr ? 1 : 0; }
  else if (getsockopt(socket, os_level, os_name, (char*)optval, &len) == SOCKET_ERROR) { err = _sppc_win_wsa_errno(); }
  else if (level == SPPC_SOL_SOCKET && optname == SPPC_SO_ERROR) { *optval = _sppc_errno_wsa(*optval); }
  else if (level == SPPC_SOL_SOCKET && optname == SPPC_SO_TYPE) {
    size_t n;
    _sppc_pair const *const types = _sppc_sock_types_table(&n);
    *optval = _sppc_from_os_value(*optval, types, n, *optval);
  }
  _sppc_ofd_release(ofd);
  return err;
}

/// The receive and send timeouts are milliseconds on Windows (0 is "never",
/// as POSIX's zero time is): a time that is not 0 but under one is one.
_gnu_inline
bool _sppc_win_is_timeout(const int level, const int name) {
  return level == SPPC_SOL_SOCKET && (name == SPPC_SO_RCVTIMEO || name == SPPC_SO_SNDTIMEO);
}

_gnu_inline _gnu_restrict_access(read_only, 4) _gnu_nonnull(4)
_sppc_api int sppc_setsockopt_timeval(const int fd, const int level, const int optname,
  sppc_timeval const *_sppc_restrict optval) {
  if (optval->usec >= 1000000u) { return SPPC_EINVAL; }
  _sppc_ofd *ofd;
  SOCKET socket;
  int os_level, os_name;
  int err = _sppc_win_sockopt(fd, level, optname, &ofd, &socket, &os_level, &os_name);
  if (err != 0) { return err; }
  int rc;
  if (_sppc_win_is_timeout(level, optname)) {
    uint64_t ms = optval->sec * 1000u + (optval->usec + 999u) / 1000u;
    if (ms > 0xFFFFFFFEu) { ms = 0xFFFFFFFEu; }
    const DWORD value = (DWORD)ms;
    rc = setsockopt(socket, os_level, os_name, (char const*)&value, (int)sizeof value);
  } else {
    struct timeval tv;
    tv.tv_sec = optval->sec > (uint64_t)LONG_MAX ? LONG_MAX : (long)optval->sec;
    tv.tv_usec = (long)optval->usec;
    rc = setsockopt(socket, os_level, os_name, (char const*)&tv, (int)sizeof tv);
  }
  if (rc == SOCKET_ERROR) { err = _sppc_win_wsa_errno(); }
  _sppc_ofd_release(ofd);
  return err;
}

_gnu_inline _gnu_restrict_access(write_only, 4) _gnu_nonnull(4)
_sppc_api int sppc_getsockopt_timeval(const int fd, const int level, const int optname,
  sppc_timeval *_sppc_restrict optval) {
  _sppc_ofd *ofd;
  SOCKET socket;
  int os_level, os_name;
  int err = _sppc_win_sockopt(fd, level, optname, &ofd, &socket, &os_level, &os_name);
  if (err != 0) { return err; }
  if (_sppc_win_is_timeout(level, optname)) {
    DWORD ms = 0;
    int len = (int)sizeof ms;
    if (getsockopt(socket, os_level, os_name, (char*)&ms, &len) == SOCKET_ERROR) { err = _sppc_win_wsa_errno(); }
    else {
      optval->sec = ms / 1000u;
      optval->usec = (ms % 1000u) * 1000u;
    }
  } else {
    struct timeval tv;
    int len = (int)sizeof tv;
    if (getsockopt(socket, os_level, os_name, (char*)&tv, &len) == SOCKET_ERROR) { err = _sppc_win_wsa_errno(); }
    else {
      optval->sec = tv.tv_sec < 0 ? 0 : (uint64_t)tv.tv_sec;
      optval->usec = tv.tv_usec < 0 ? 0 : (uint64_t)tv.tv_usec;
    }
  }
  _sppc_ofd_release(ofd);
  return err;
}

/// Windows' linger counts in a short: a longer one is the longest it takes.
_gnu_inline _gnu_restrict_access(read_only, 4) _gnu_nonnull(4)
_sppc_api int sppc_setsockopt_linger(const int fd, const int level, const int optname,
  sppc_linger const *_sppc_restrict optval) {
  _sppc_ofd *ofd;
  SOCKET socket;
  int os_level, os_name;
  int err = _sppc_win_sockopt(fd, level, optname, &ofd, &socket, &os_level, &os_name);
  if (err != 0) { return err; }
  struct linger lg;
  lg.l_onoff = (u_short)(optval->onoff != 0);
  lg.l_linger = (u_short)(optval->linger < 0 ? 0 : optval->linger > 0xFFFF ? 0xFFFF : optval->linger);
  if (setsockopt(socket, os_level, os_name, (char const*)&lg, (int)sizeof lg) == SOCKET_ERROR) { err = _sppc_win_wsa_errno(); }
  _sppc_ofd_release(ofd);
  return err;
}

_gnu_inline _gnu_restrict_access(write_only, 4) _gnu_nonnull(4)
_sppc_api int sppc_getsockopt_linger(const int fd, const int level, const int optname,
  sppc_linger *_sppc_restrict optval) {
  _sppc_ofd *ofd;
  SOCKET socket;
  int os_level, os_name;
  int err = _sppc_win_sockopt(fd, level, optname, &ofd, &socket, &os_level, &os_name);
  if (err != 0) { return err; }
  struct linger lg;
  int len = (int)sizeof lg;
  if (getsockopt(socket, os_level, os_name, (char*)&lg, &len) == SOCKET_ERROR) { err = _sppc_win_wsa_errno(); }
  else {
    optval->onoff = lg.l_onoff;
    optval->linger = lg.l_linger;
  }
  _sppc_ofd_release(ofd);
  return err;
}

// ---------- readiness ----------

/// Which of "events" (sppc's POLL*) a descriptor that is not a socket has
/// now, Windows having no poll for those: a disk file is always ready; a
/// pipe has input once "PeekNamedPipe" sees some, and has hung up once its
/// writers have gone; a console has input once it holds events; any end
/// that writes is taken as writable. Whether one may still change is
/// "waits".
_gnu_inline _gnu_nonnull(1, 3)
int _sppc_win_ready(_sppc_ofd *ofd, const int events, bool *waits) {
  const int in = events & (SPPC_POLLIN | SPPC_POLLRDNORM);
  const int out = events & (SPPC_POLLOUT | SPPC_POLLWRNORM);
  if (ofd->disk) { return in | out; }
  int ready = 0;
  if (ofd->kind == _SPPC_FD_CONSOLE_IN) {
    DWORD pending = 0;
    if (GetNumberOfConsoleInputEvents(ofd->handle, &pending) && pending > 0) { ready |= in; }
    else if (in) { *waits = true; }
  } else if (ofd->kind == _SPPC_FD_CONSOLE_OUT) {
    ready |= out;
  } else {
    if (ofd->access & _SPPC_FD_READ) {
      DWORD avail = 0;
      if (!PeekNamedPipe(ofd->handle, NULL, 0, NULL, &avail, NULL)) { ready |= SPPC_POLLHUP; }
      else if (avail > 0) { ready |= in; }
      else if (in) { *waits = true; }
    }
    if (ofd->access & _SPPC_FD_WRITE) { ready |= out; }
  }
  return ready;
}

/// POSIX's poll, any descriptor: sockets through "WSAPoll", the rest as
/// "_sppc_win_ready" sees them. While some descriptor of the rest could
/// still become ready, the sockets are waited on in slices of 10 ms, and the
/// rest looked at between them. A negative fd is skipped; one that is not
/// open is POLLNVAL.
_gnu_inline _gnu_restrict_access(read_write, 1) _gnu_restrict_access(write_only, 4) _gnu_nonnull(4)
_sppc_api int sppc_poll(sppc_pollfd *_sppc_restrict fds, const uint64_t count, const int timeout, int *_sppc_restrict out_n) {
  *out_n = -1;
  size_t n;
  _sppc_pair const *const table = _sppc_poll_table(&n);
  if (count > 65536) { return SPPC_EINVAL; }
  for (uint64_t i = 0; i < count; ++i) {
    int unused;
    if (_sppc_to_os_flags(fds[i].events, table, n, &unused) != 0) { return SPPC_EINVAL; }
  }

  _sppc_ofd **const ofds = count > 0 ? (_sppc_ofd**)calloc((size_t)count, sizeof *ofds) : NULL;
  WSAPOLLFD *const sockets = count > 0 ? (WSAPOLLFD*)calloc((size_t)count, sizeof *sockets) : NULL;
  uint64_t *const which = count > 0 ? (uint64_t*)calloc((size_t)count, sizeof *which) : NULL;
  if (count > 0 && (ofds == NULL || sockets == NULL || which == NULL)) {
    free(ofds);
    free(sockets);
    free(which);
    return SPPC_ENOMEM;
  }
  ULONG nsockets = 0;
  for (uint64_t i = 0; i < count; ++i) {
    fds[i].revents = 0;
    if (fds[i].fd < 0) { continue; }
    ofds[i] = _sppc_fd_get(fds[i].fd);
    if (ofds[i] == NULL) { fds[i].revents = SPPC_POLLNVAL; }
    else if (ofds[i]->kind == _SPPC_FD_SOCKET) {
      int events = 0;
      _sppc_to_os_flags(fds[i].events, table, n, &events);
      sockets[nsockets].fd = (SOCKET)(uintptr_t)ofds[i]->handle;
      sockets[nsockets].events = (SHORT)(events & ~POLLPRI); // Winsock rejects POLLPRI (it has no urgent data)
      which[nsockets++] = i;
    }
  }

  const ULONGLONG start = GetTickCount64();
  int err = 0, ready = 0;
  for (;;) {
    ready = 0;
    bool waits = false;
    for (uint64_t i = 0; i < count; ++i) {
      if (ofds[i] != NULL && ofds[i]->kind != _SPPC_FD_SOCKET) { fds[i].revents = (int16_t)_sppc_win_ready(ofds[i], fds[i].events, &waits); }
      if (fds[i].revents != 0) { ++ready; }
    }
    int left = INT_MAX;
    if (timeout >= 0) {
      const ULONGLONG gone = GetTickCount64() - start;
      left = gone >= (ULONGLONG)timeout ? 0 : (int)((ULONGLONG)timeout - gone);
    }
    const int slice = ready > 0 ? 0 : waits ? (left < 10 ? left : 10) : timeout < 0 ? -1 : left;
    if (nsockets > 0) {
      const int got = WSAPoll(sockets, nsockets, slice);
      if (got == SOCKET_ERROR) {
        err = _sppc_win_wsa_errno();
        break;
      }
      for (ULONG k = 0; k < nsockets; ++k) {
        const uint64_t i = which[k];
        int16_t revents = (int16_t)_sppc_from_os_flags(sockets[k].revents, table, n);
        // Winsock's POLLIN is POLLRDNORM | POLLRDBAND, and it reports just
        // the half that happened, so either is POLLIN here.
        if ((sockets[k].revents & POLLIN) != 0) { revents |= (int16_t)(fds[i].events & SPPC_POLLIN); }
        fds[i].revents = revents;
        if (fds[i].revents != 0) { ++ready; }
      }
    } else if (ready == 0 && slice != 0) {
      Sleep(slice < 0 ? INFINITE : (DWORD)slice);
    }
    if (ready > 0 || left == 0 || (!waits && nsockets > 0) || (!waits && slice != 0)) { break; }
  }
  for (uint64_t i = 0; i < count; ++i) { _sppc_ofd_release(ofds[i]); }
  free(ofds);
  free(sockets);
  free(which);
  if (err != 0) { return err; }
  *out_n = ready;
  return 0;
}

/// POSIX's select, through "sppc_poll": a descriptor is readable when it
/// has input, has hung up or has an error, writable when it takes output or
/// has an error, exceptional with urgent data. EBADF for a set naming a
/// descriptor that is not open. The time left goes back in "timeout", as
/// Linux's does.
_gnu_inline _gnu_restrict_access(read_write, 2) _gnu_restrict_access(read_write, 3)
_gnu_restrict_access(read_write, 4) _gnu_restrict_access(read_write, 5) _gnu_restrict_access(write_only, 6) _gnu_nonnull(6)
_sppc_api int sppc_select(const int nfds, sppc_fd_set *_sppc_restrict readfds, sppc_fd_set *_sppc_restrict writefds,
  sppc_fd_set *_sppc_restrict exceptfds, sppc_timeval *_sppc_restrict timeout, int *_sppc_restrict out_n) {
  *out_n = -1;
  if (nfds < 0 || nfds > 1024 || (timeout != NULL && timeout->usec >= 1000000u)) { return SPPC_EINVAL; }
  sppc_fd_set *const sets[3] = {readfds, writefds, exceptfds};
  static int16_t const asks[3] = {SPPC_POLLIN, SPPC_POLLOUT, SPPC_POLLPRI};
  sppc_pollfd fds[1024];
  int count = 0;
  for (int fd = 0; fd < nfds; ++fd) {
    int16_t events = 0;
    for (int s = 0; s < 3; ++s) {
      if (sets[s] != NULL && (sets[s]->bits[fd / 64] >> (fd % 64) & 1u)) { events |= asks[s]; }
    }
    if (events != 0) {
      fds[count].fd = fd;
      fds[count].events = events;
      fds[count].revents = 0;
      ++count;
    }
  }
  int ms = -1;
  if (timeout != NULL) {
    const uint64_t want = timeout->sec * 1000u + (timeout->usec + 999u) / 1000u;
    ms = want > INT_MAX ? INT_MAX : (int)want;
  }
  const ULONGLONG start = GetTickCount64();
  int got = 0;
  const int err = sppc_poll(fds, (uint64_t)count, ms, &got);
  if (err != 0) { return err; }
  for (int k = 0; k < count; ++k) {
    if (fds[k].revents & SPPC_POLLNVAL) { return SPPC_EBADF; }
  }
  for (int s = 0; s < 3; ++s) {
    if (sets[s] != NULL) { memset(sets[s], 0, sizeof *sets[s]); }
  }
  int total = 0;
  static int16_t const meets[3] = {
    SPPC_POLLIN | SPPC_POLLRDNORM | SPPC_POLLHUP | SPPC_POLLERR,
    SPPC_POLLOUT | SPPC_POLLWRNORM | SPPC_POLLERR,
    SPPC_POLLPRI | SPPC_POLLRDBAND,
  };
  for (int k = 0; k < count; ++k) {
    for (int s = 0; s < 3; ++s) {
      if (sets[s] != NULL && (fds[k].events & asks[s]) && (fds[k].revents & meets[s])) {
        sets[s]->bits[fds[k].fd / 64] |= (uint64_t)1 << (fds[k].fd % 64);
        ++total;
      }
    }
  }
  if (timeout != NULL) {
    const ULONGLONG gone = GetTickCount64() - start;
    const uint64_t left = (uint64_t)ms > gone ? (uint64_t)ms - gone : 0;
    timeout->sec = left / 1000u;
    timeout->usec = (left % 1000u) * 1000u;
  }
  *out_n = total;
  return 0;
}

// ==================== PROCESSES AND ENVIRONMENT ====================

/// A variable's name as POSIX takes it: not empty, and without an '='
/// (EINVAL), as UTF-16 in a block the caller frees.
_gnu_inline _gnu_nonnull(1, 2)
int _sppc_win_env_name(char const *key, wchar_t **out) {
  *out = NULL;
  if (*key == '\0' || strchr(key, '=') != NULL) { return SPPC_EINVAL; }
  const int err = _sppc_win_wpath(key, out);
  return err == SPPC_EILSEQ ? SPPC_EINVAL : err;
}

/// The environment is Windows' own, in UTF-16, read and written with its
/// "W" calls and crossing as UTF-8: the C library's copy is in the ANSI code
/// page, and would garble anything else. So the C library's "getenv" does
/// not see what "sppc_setenv" sets; a child process will, once sppc starts
/// them. ENOENT for a variable that is not set; ERANGE when its value and
/// terminator do not fit (either way "out" is emptied).
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(write_only, 2, 3) _gnu_nonnull(1, 2)
_sppc_api int sppc_getenv(char const *_sppc_restrict key, char *_sppc_restrict out, const size_t size) {
  if (size == 0) { return SPPC_ERANGE; }
  *out = '\0';
  wchar_t *name;
  const int bad = _sppc_win_env_name(key, &name);
  if (bad != 0) { return bad == SPPC_EINVAL ? SPPC_ENOENT : bad; }
  int err = 0;
  SetLastError(0);
  const DWORD need = GetEnvironmentVariableW(name, NULL, 0);
  wchar_t *const value = need > 0 ? (wchar_t*)malloc(need * sizeof *value) : NULL;
  if (need == 0) {
    // An empty value is a variable that is set; only a missing one says so.
    err = GetLastError() == ERROR_ENVVAR_NOT_FOUND ? SPPC_ENOENT : 0;
  } else if (value == NULL) {
    err = SPPC_ENOMEM;
  } else {
    GetEnvironmentVariableW(name, value, need);
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, value, -1, NULL, 0, NULL, NULL);
    if (bytes <= 0 || (size_t)bytes > size) { err = bytes <= 0 ? SPPC_EILSEQ : SPPC_ERANGE; }
    else { WideCharToMultiByte(CP_UTF8, 0, value, -1, out, bytes, NULL, NULL); }
  }
  free(value);
  free(name);
  return err;
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(read_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_setenv(char const *_sppc_restrict key, char const *_sppc_restrict val, const bool overwrite) {
  wchar_t *name, *value;
  int err = _sppc_win_env_name(key, &name);
  if (err != 0) { return err; }
  // An empty value is a value, which the conversion (made for paths) refuses.
  if (*val == '\0') {
    value = (wchar_t*)calloc(1, sizeof *value);
    err = value == NULL ? SPPC_ENOMEM : 0;
  } else {
    err = _sppc_win_wpath(val, &value);
    if (err == SPPC_EILSEQ) { err = SPPC_EINVAL; }
  }
  if (err == 0) {
    SetLastError(0);
    const bool exists = GetEnvironmentVariableW(name, NULL, 0) > 0 || GetLastError() != ERROR_ENVVAR_NOT_FOUND;
    if ((overwrite || !exists) && !SetEnvironmentVariableW(name, value)) { err = _sppc_errno_win32(GetLastError()); }
    free(value);
  }
  free(name);
  return err;
}

/// A variable that is not set is no error, as POSIX.
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_nonnull(1)
_sppc_api int sppc_unsetenv(char const *_sppc_restrict key) {
  wchar_t *name;
  int err = _sppc_win_env_name(key, &name);
  if (err != 0) { return err; }
  if (!SetEnvironmentVariableW(name, NULL) && GetLastError() != ERROR_ENVVAR_NOT_FOUND) {
    err = _sppc_errno_win32(GetLastError());
  }
  free(name);
  return err;
}

_gnu_inline _gnu_restrict_access(write_only, 1) _gnu_nonnull(1)
_sppc_api void sppc_get_pid(int32_t *_sppc_restrict out_pid) {
  *out_pid = (int32_t)GetCurrentProcessId();
}

/// What "NtQueryInformationProcess" fills in for ProcessBasicInformation;
/// the SDK has it only in the driver kit.
typedef struct _sppc_win_process_info {
  LONG exit_status;
  PVOID peb;
  ULONG_PTR affinity;
  LONG priority;
  ULONG_PTR pid;
  ULONG_PTR parent;
} _sppc_win_process_info;

typedef LONG (WINAPI *_sppc_win_query_process)(HANDLE, ULONG, PVOID, ULONG, PULONG);

/// The process that started this one. Windows does not give a process
/// whose parent ended to another, as POSIX does to init, so this may be the
/// id of one that is gone (or by now of another that took its number); 0
/// if Windows will not say.
_gnu_inline _gnu_restrict_access(write_only, 1) _gnu_nonnull(1)
_sppc_api void sppc_get_ppid(int32_t *_sppc_restrict out_ppid) {
  *out_ppid = 0;
  const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  _sppc_win_query_process query = NULL;
  if (ntdll != NULL) {
    FARPROC const found = GetProcAddress(ntdll, "NtQueryInformationProcess");
    memcpy(&query, &found, sizeof query);
  }
  _sppc_win_process_info info;
  if (query != NULL && query(GetCurrentProcess(), 0, &info, sizeof info, NULL) >= 0) { *out_ppid = (int32_t)info.parent; }
}

/// What Windows can do of POSIX's "kill": 0 asks whether the process is
/// there (ESRCH if not, EPERM if this one may not touch it); SIGKILL and
/// SIGTERM end it, with the exit code a shell gives a process ended so
/// (128 plus the signal). Windows has no other signals for one process to
/// send another: ENOSYS for them, EINVAL for a number that is none. A
/// process group (a pid of 0 or below) it has no notion of: ENOSYS.
_gnu_inline
_sppc_api int sppc_signal(const int32_t pid, const int signal) {
  if (signal < 0 || signal > SPPC_SIGSYS || signal == 16 || signal == 30) { return SPPC_EINVAL; }
  if (signal != 0 && signal != SPPC_SIGKILL && signal != SPPC_SIGTERM) { return SPPC_ENOSYS; }
  if (pid <= 0) { return SPPC_ENOSYS; }
  const HANDLE h = OpenProcess(signal == 0 ? PROCESS_QUERY_LIMITED_INFORMATION : PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION,
    FALSE, (DWORD)pid);
  if (h == NULL) {
    const DWORD win = GetLastError();
    return win == ERROR_INVALID_PARAMETER ? SPPC_ESRCH : win == ERROR_ACCESS_DENIED ? SPPC_EPERM : _sppc_errno_win32(win);
  }
  int err = 0;
  DWORD code = 0;
  if (GetExitCodeProcess(h, &code) && code != STILL_ACTIVE) { err = SPPC_ESRCH; }
  else if (signal != 0 && !TerminateProcess(h, 128u + (unsigned)signal)) { err = _sppc_errno_win32(GetLastError()); }
  CloseHandle(h);
  return err;
}

// ==================== PROCESS HARDENING ====================

/// Windows' Arbitrary Code Guard: from now on this process can neither make
/// memory executable that was not (nor allocate it so) nor make code
/// writable, as Linux's MDWE denies, for good. It cannot be turned off.
_gnu_inline _gnu_cold
_sppc_api int sppc_deny_wx(void) {
  PROCESS_MITIGATION_DYNAMIC_CODE_POLICY policy;
  memset(&policy, 0, sizeof policy);
  policy.ProhibitDynamicCode = 1;
  if (SetProcessMitigationPolicy(ProcessDynamicCodePolicy, &policy, sizeof policy)) { return 0; }
  // "fputs", not "fprintf": mingw's is a static wrapper, which an inline
  // function with external linkage may not call.
  const DWORD win = GetLastError();
  fputs("[sppc] could not deny writable-and-executable memory: Windows refused Arbitrary Code Guard.\n"
    "  To run without it, set 'deny_wx = false' under [memory.map] in spp.toml and rebuild.\n", stderr);
  return _sppc_errno_win32(win);
}

_gnu_inline _gnu_cold
_sppc_api int sppc_seal_segments(void) {
  fputs("[sppc] could not seal the program's memory mappings: Windows has no mseal\n"
    "  To run without it, set 'seal = false' under [memory.map] in spp.toml and rebuild.\n", stderr);
  return SPPC_ENOSYS;
}

// ==================== EVERY PLATFORM'S ====================

#include <sppc/platform/common.h>
