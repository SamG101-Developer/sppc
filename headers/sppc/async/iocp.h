#pragma once

/// The Windows i/o backend: an i/o completion port per thread, the same
/// completion model as Linux's io_uring. A task starts an operation with an
/// OVERLAPPED of its own, the task parks, and the scheduler's poll takes
/// finished operations off the port ("GetQueuedCompletionStatusEx") and
/// makes their tasks runnable again; the task then reads its result with
/// "WSAGetOverlappedResult" as the call it stands in for would have given it.
///
/// What goes through the port is platform/windows.h's to say (its sockets):
/// this file gives it the port, a handle tied to it ("_gt_iocp_attach"), the
/// running task's OVERLAPPED ("_gt_iocp_overlapped") and the wait
/// ("_gt_iocp_wait"). A handle is tied to one port for good, so a socket
/// goes through the port of the thread that first waits on it, and any
/// other thread's calls on it block that thread, as without a backend.
///
/// Sleeps wait on the port too: the earliest deadline sets a high-resolution
/// waitable timer, whose expiry ntdll's wait completion packets post to the
/// port, so a 2 ms sleep is not rounded up to the 15.6 ms scheduler tick a
/// port's own timeout counts in. Without those (Windows before 8) the
/// timeout is the port's.

#include <sppc/async/sched.h>

/// The completion key of the sleep timer's packet; an operation's is 0.
#define _GT_IOCP_TIMER_KEY ((ULONG_PTR)1)

/// Completions taken off the port per call.
#define _GT_IOCP_BATCH 64

/// "Do not wait" and "no deadline", for "_gt_iocp_poll".
#define _GT_IOCP_NOW INT64_MIN
#define _GT_IOCP_NEVER INT64_MAX

// Defined once, in sppc.c: the last runtime id handed out.
extern volatile LONG64 _gt_iocp_ids;

// ==================== SETUP ====================

_gnu_inline _gnu_cold
void _gt_io_setup(void) {
  memset(&_gt_R.io, 0, sizeof _gt_R.io);
  _gt_R.inflight = 0;

  // One thread's port: it is only ever waited on by its own thread.
  _gt_R.io.port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
  if (_gt_R.io.port == NULL) { return; }
  _gt_R.io.id = InterlockedIncrement64(&_gt_iocp_ids);

  // The sleep timer, and the packet that posts its expiry to the port.
  _gt_R.io.timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
  if (_gt_R.io.timer == NULL) { _gt_R.io.timer = CreateWaitableTimerExW(NULL, NULL, 0, TIMER_ALL_ACCESS); }
  const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  typedef LONG (NTAPI *create_fn)(HANDLE *packet, ACCESS_MASK access, void *attributes);
  create_fn create = NULL;
  if (ntdll != NULL && _gt_R.io.timer != NULL) {
    FARPROC const c = GetProcAddress(ntdll, "NtCreateWaitCompletionPacket");
    FARPROC const a = GetProcAddress(ntdll, "NtAssociateWaitCompletionPacket");
    FARPROC const x = GetProcAddress(ntdll, "NtCancelWaitCompletionPacket");
    memcpy(&create, &c, sizeof create);
    memcpy(&_gt_R.io.associate, &a, sizeof _gt_R.io.associate);
    memcpy(&_gt_R.io.cancel, &x, sizeof _gt_R.io.cancel);
  }
  if (create == NULL || _gt_R.io.associate == NULL || _gt_R.io.cancel == NULL
    || create(&_gt_R.io.packet, GENERIC_ALL, NULL) < 0) {
    _gt_R.io.packet = NULL;
  }
  _gt_R.io.ok = 1;
}

/// Outstanding operations are cancelled and waited out before the port
/// goes: the kernel writes each one's result into its task's OVERLAPPED,
/// which is about to be freed with the task.
_gnu_inline _gnu_cold
void _gt_io_teardown(void) {
  if (!_gt_R.io.ok) { return; }
  unsigned outstanding = 0;
  for (uint32_t i = 0; i < _gt_R.nslots; ++i) {
    gt_task *const t = _gt_R.slots[i];
    if (t != NULL && t->io.handle != NULL) {
      CancelIoEx(t->io.handle, &t->io.ov);
      ++outstanding;
    }
  }
  while (outstanding > 0) {
    OVERLAPPED_ENTRY done[_GT_IOCP_BATCH];
    ULONG n = 0;
    if (!GetQueuedCompletionStatusEx(_gt_R.io.port, done, _GT_IOCP_BATCH, &n, INFINITE, FALSE)) { break; }
    for (ULONG i = 0; i < n; ++i) {
      if (done[i].lpCompletionKey != _GT_IOCP_TIMER_KEY) { --outstanding; }
    }
  }
  if (_gt_R.io.packet != NULL) {
    _gt_R.io.cancel(_gt_R.io.packet, TRUE);
    CloseHandle(_gt_R.io.packet);
  }
  if (_gt_R.io.timer != NULL) { CloseHandle(_gt_R.io.timer); }
  CloseHandle(_gt_R.io.port);
  memset(&_gt_R.io, 0, sizeof _gt_R.io);
  _gt_R.inflight = 0;
}

// ==================== WAITING ====================

/// The monotonic clock, in ns.
_gnu_inline
int64_t _gt_iocp_now(void) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (int64_t)now.tv_sec * 1000000000LL + now.tv_nsec;
}

/// Set the sleep timer to go off "left" ns from now, its expiry posted to
/// the port; false if it cannot be (no packets), or has gone off already.
_gnu_inline
bool _gt_iocp_arm(const int64_t left) {
  if (_gt_R.io.packet == NULL) { return false; }
  LARGE_INTEGER due;
  due.QuadPart = -((left + 99) / 100); // negative: relative, in 100 ns
  if (!SetWaitableTimer(_gt_R.io.timer, &due, 0, NULL, NULL, FALSE)) { return false; }
  if (_gt_R.io.armed) { return true; } // still tied to the port, now at the new time
  BOOLEAN already = FALSE;
  if (_gt_R.io.associate(_gt_R.io.packet, _gt_R.io.port, _gt_R.io.timer, (PVOID)_GT_IOCP_TIMER_KEY, NULL, 0, 0,
    &already) < 0 || already) {
    return false;
  }
  _gt_R.io.armed = true;
  return true;
}

/// Take what has finished off the port, waiting for something no later than
/// "until" (a monotonic deadline in ns, or "_GT_IOCP_NOW" / "_GT_IOCP_NEVER")
/// or the earliest sleeper's; then wake every sleeper whose time has come.
_gnu_inline
void _gt_iocp_poll(int64_t until) {
  if (_gt_R.io.sleepers != NULL && _gt_R.io.sleepers->io.deadline < until) { until = _gt_R.io.sleepers->io.deadline; }
  DWORD ms = 0;
  if (until == _GT_IOCP_NEVER) { ms = INFINITE; }
  else if (until != _GT_IOCP_NOW) {
    const int64_t left = until - _gt_iocp_now();
    if (left <= 0) { ms = 0; }
    else if (_gt_iocp_arm(left)) { ms = INFINITE; }
    else { ms = left >= 0xFFFFFFFELL * 1000000LL ? 0xFFFFFFFE : (DWORD)((left + 999999) / 1000000); }
  }

  OVERLAPPED_ENTRY done[_GT_IOCP_BATCH];
  ULONG n = 0;
  if (GetQueuedCompletionStatusEx(_gt_R.io.port, done, _GT_IOCP_BATCH, &n, ms, FALSE)) {
    for (ULONG i = 0; i < n; ++i) {
      if (done[i].lpCompletionKey == _GT_IOCP_TIMER_KEY) {
        _gt_R.io.armed = false;
        continue;
      }
      // The OVERLAPPED is the first member of the task's i/o state.
      gt_task *const t = (gt_task*)((char*)done[i].lpOverlapped - offsetof(gt_task, io));
      t->io.handle = NULL;
      _gt_R.inflight--;
      _gt_rq_push(t);
    }
  }

  if (_gt_R.io.sleepers != NULL) {
    const int64_t now = _gt_iocp_now();
    while (_gt_R.io.sleepers != NULL && _gt_R.io.sleepers->io.deadline <= now) {
      gt_task *const t = _gt_R.io.sleepers;
      _gt_R.io.sleepers = t->io.next_sleeper;
      _gt_R.inflight--;
      _gt_rq_push(t);
    }
  }
}

/// Collect finished operations and make their tasks runnable again; with
/// "block" set, wait for at least one first.
_gnu_inline _gnu_hot
void _gt_io_poll(const int block) {
  if (!_gt_R.io.ok || _gt_R.inflight == 0) { return; }
  _gt_iocp_poll(block ? _GT_IOCP_NEVER : _GT_IOCP_NOW);
}

/// Wait for a completion no later than an absolute deadline on "clock",
/// timed on the monotonic clock (the caller checks the deadline again).
_gnu_inline _gnu_nonnull(2)
void _gt_io_poll_until(const clockid_t clock, struct timespec const *deadline) {
  if (!_gt_R.io.ok) { return; }
  const struct timespec left = _gt_remaining(clock, deadline);
  _gt_iocp_poll(_gt_iocp_now() + (int64_t)left.tv_sec * 1000000000LL + left.tv_nsec);
}

// ==================== OPERATIONS ====================

_gnu_inline _gnu_hot
int _gt_io_ready(void) {
  return _gt_live() && _gt_R.io.ok;
}

/// Tie "handle" to this thread's port, as "owner" records (a socket's
/// description keeps it): true if its operations may go through the port,
/// tying it now if no thread has yet; false for one tied to another
/// thread's port, or that could not be tied.
_gnu_inline _gnu_nonnull(2)
bool _gt_iocp_attach(HANDLE handle, volatile LONG64 *owner) {
  if (!_gt_io_ready()) { return false; }
  const LONG64 me = _gt_R.io.id;
  const LONG64 was = InterlockedCompareExchange64(owner, -1, 0); // -1: being tied, or never to be
  if (was != 0) { return was == me; }
  if (CreateIoCompletionPort(handle, _gt_R.io.port, 0, 0) == NULL) { return false; }
  InterlockedExchange64(owner, me);
  return true;
}

/// The running task's OVERLAPPED, cleared for an operation to start with.
_gnu_inline
OVERLAPPED* _gt_iocp_overlapped(void) {
  gt_task *const self = _gt_R.current;
  memset(&self->io.ov, 0, sizeof self->io.ov);
  return &self->io.ov;
}

/// Park the running task until the operation it started on "handle" with
/// its OVERLAPPED has finished (its packet taken off the port). Every
/// operation that started (succeeded at once, or is pending) posts one.
_gnu_inline
void _gt_iocp_wait(HANDLE handle) {
  gt_task *const self = _gt_R.current;
  self->io.handle = handle;
  _gt_R.inflight++;
  _gt_block(GT_STATE_WAITING);
}

/// Park the running task until the monotonic clock reaches "deadline" (ns).
_gnu_inline
void _gt_iocp_sleep_until(const int64_t deadline) {
  gt_task *const self = _gt_R.current;
  self->io.deadline = deadline;
  gt_task **at = &_gt_R.io.sleepers;
  while (*at != NULL && (*at)->io.deadline <= deadline) { at = &(*at)->io.next_sleeper; }
  self->io.next_sleeper = *at;
  *at = self;
  _gt_R.inflight++;
  _gt_block(GT_STATE_WAITING);
}

/// A sleep, the rest of the thread's tasks running meanwhile. Never ends
/// early: a relative sleep is timed on the monotonic clock (as POSIX's,
/// which setting the real-time clock leaves alone); an absolute one on the
/// real-time clock is waited for on the monotonic one and looked at again
/// on waking, until the real-time clock has reached it. Anything this
/// cannot time (another clock, a time that is none) is the plain call's.
_gnu_inline _gnu_nonnull(3, 4)
int _gt_try_sleep(const clockid_t clock, const int flags, struct timespec const *duration, int *res) {
  if (!_gt_io_ready() || (clock != CLOCK_MONOTONIC && clock != CLOCK_REALTIME)
    || duration->tv_sec < 0 || duration->tv_nsec < 0 || duration->tv_nsec >= 1000000000L) {
    return 0;
  }
  // At most a century ahead, so the ns count cannot overflow.
  const int64_t sec = duration->tv_sec < 3153600000LL ? (int64_t)duration->tv_sec : 3153600000LL;
  const int64_t ns = sec * 1000000000LL + duration->tv_nsec;
  if ((flags & TIMER_ABSTIME) == 0) { _gt_iocp_sleep_until(_gt_iocp_now() + ns); }
  else if (clock == CLOCK_MONOTONIC) { _gt_iocp_sleep_until(ns); }
  else {
    for (;;) {
      struct timespec now;
      clock_gettime(CLOCK_REALTIME, &now);
      const int64_t left = ns - ((int64_t)now.tv_sec * 1000000000LL + now.tv_nsec);
      if (left <= 0) { break; }
      _gt_iocp_sleep_until(_gt_iocp_now() + left);
    }
  }
  *res = 0;
  return 1;
}
