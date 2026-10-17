#pragma once

/// The Linux i/o backend. Blocking calls made from a task are submitted to an
/// io_uring and the task parks until the completion arrives, so the rest of
/// the thread keeps running. Every operation also has a plain-syscall
/// fallback, taken when the ring is unavailable or full ("_gt_try_*"
/// returning 0).

#include <sppc/async/sched.h>
#include <linux/io_uring.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

// ==================== RING ====================

/// Raw syscalls rather than liburing: the runtime needs
/// six opcodes and one queue, and taking a dependency for
/// that would put a library between the scheduler and the
/// only thing it blocks on.
#define GT_RING_ENTRIES 256u

/// Wrapper to enter the io_uring task and pull the result
/// out through the return int of the syscall.
_gnu_inline
int _gt_io_enter(const unsigned to_submit, const unsigned min_complete, const unsigned flags) {
  return (int)syscall(__NR_io_uring_enter, _gt_R.io.fd, to_submit, min_complete, flags, NULL, 0);
}

/// Setup the runtime's "ring" object with all data required
/// for a usable io_uring async implementation in the green
/// thread runtime.
_gnu_inline _gnu_cold
void _gt_io_setup(void) {
  struct io_uring_params params;
  memset(&params, 0, sizeof params);

  // A kernel without io_uring, or a sandbox that has taken it
  // away, is not an error: every i/o path falls back to the
  // blocking syscall it would have made anyway.
  const int fd = (int)syscall(__NR_io_uring_setup, GT_RING_ENTRIES, &params);
  if (fd < 0) {
    _gt_R.io.ok = 0;
    return;
  }

  // The "CQ" bytes are the completion ring, the "SQ" bytes are
  // the submission ring. These are used by the ring for normal
  // operations (after mmap).
  size_t sq_bytes = (size_t)params.sq_off.array + params.sq_entries * sizeof(unsigned);
  size_t cq_bytes = (size_t)params.cq_off.cqes + params.cq_entries * sizeof(struct io_uring_cqe);
  const bool single = (params.features & IORING_FEAT_SINGLE_MMAP) != 0;
  if (single) {
    if (cq_bytes > sq_bytes) { sq_bytes = cq_bytes; }
    cq_bytes = sq_bytes;
  }

  // Build the sequential ring from the SQ bytes, checking for
  // allocation failures.
  void *const sq = mmap(
    NULL, sq_bytes, PROT_READ | PROT_WRITE,
    MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQ_RING);
  if (sq == MAP_FAILED) {
    close(fd);
    _gt_R.io.ok = 0;
    return;
  }

  // Assuming a single mapping, default the completion ring to
  // sequential ring, and then do the manual re-mapping if we
  // are otherwise.
  void *cq = sq;
  if (!single) {
    cq = mmap(
      NULL, cq_bytes, PROT_READ | PROT_WRITE,
      MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_CQ_RING);
    if (cq == MAP_FAILED) {
      munmap(sq, sq_bytes);
      close(fd);
      _gt_R.io.ok = 0;
      return;
    }
  }

  // Finally, the SQE (sequential ring entries), whose byte
  // size is the number of SQ entries and the additional struct
  // size.
  const size_t sqe_bytes = params.sq_entries * sizeof(struct io_uring_sqe);
  void *const sqes = mmap(
    NULL, sqe_bytes, PROT_READ | PROT_WRITE,
    MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQES);
  if (sqes == MAP_FAILED) {
    if (!single) { munmap(cq, cq_bytes); }
    munmap(sq, sq_bytes);
    close(fd);
    _gt_R.io.ok = 0;
    return;
  }

  // Set up the entire ring, with all the SQ/CQ/SQE data, and
  // offsets for the pointers, ie pointing to sections within
  // the SQ/CQ data.
  _gt_R.io.fd = fd;
  _gt_R.io.sq_mmap = sq;
  _gt_R.io.cq_mmap = single ? NULL : cq;
  _gt_R.io.sq_bytes = sq_bytes;
  _gt_R.io.cq_bytes = cq_bytes;
  _gt_R.io.sqe_bytes = sqe_bytes;
  _gt_R.io.sq_entries = params.sq_entries;
  _gt_R.io.sqes = (struct io_uring_sqe*)sqes;
  _gt_R.io.sq_head = (unsigned*)((char*)sq + params.sq_off.head);
  _gt_R.io.sq_tail = (unsigned*)((char*)sq + params.sq_off.tail);
  _gt_R.io.sq_mask = (unsigned*)((char*)sq + params.sq_off.ring_mask);
  _gt_R.io.sq_array = (unsigned*)((char*)sq + params.sq_off.array);
  _gt_R.io.cq_head = (unsigned*)((char*)cq + params.cq_off.head);
  _gt_R.io.cq_tail = (unsigned*)((char*)cq + params.cq_off.tail);
  _gt_R.io.cq_mask = (unsigned*)((char*)cq + params.cq_off.ring_mask);
  _gt_R.io.cqes = (struct io_uring_cqe*)((char*)cq + params.cq_off.cqes);
  _gt_R.inflight = 0;
  _gt_R.io.ok = 1;
}

/// Tear down the green thread environment, unmapping the
/// SQ/CQ memory for the runtime, and cleaning up the io_uring
/// metadata too.
_gnu_inline _gnu_cold
void _gt_io_teardown(void) {
  if (!_gt_R.io.ok) { return; }
  munmap(_gt_R.io.sqes, _gt_R.io.sqe_bytes);
  if (_gt_R.io.cq_mmap) { munmap(_gt_R.io.cq_mmap, _gt_R.io.cq_bytes); }
  munmap(_gt_R.io.sq_mmap, _gt_R.io.sq_bytes);
  close(_gt_R.io.fd);
  memset(&_gt_R.io, 0, sizeof _gt_R.io);
  _gt_R.inflight = 0;
}

/// Claim a submission slot. The runtime is single-threaded, so
/// the only writer of the tail is this code and the only reader
/// that matters is the kernel - hence the release store, which
/// is what publishes the entry to it.
_gnu_inline _gnu_hot
struct io_uring_sqe* _gt_sqe(void) {
  // Get the tail and head, and do a validity check that the
  // entries are set up correctly.
  const unsigned tail = *_gt_R.io.sq_tail;
  const unsigned head = __atomic_load_n(_gt_R.io.sq_head, __ATOMIC_ACQUIRE);
  if (_sppc_unlikely(tail - head >= _gt_R.io.sq_entries)) { return NULL; }

  // Todo: Document this section.
  const unsigned idx = tail & *_gt_R.io.sq_mask;
  struct io_uring_sqe *const sqe = &_gt_R.io.sqes[idx];
  memset(sqe, 0, sizeof *sqe);
  _gt_R.io.sq_array[idx] = idx;
  __atomic_store_n(_gt_R.io.sq_tail, tail + 1, __ATOMIC_RELEASE);
  return sqe;
}

/// Collect finished operations and make their tasks runnable
/// again. With "block" set this waits for at least one, which
/// is what the scheduler does when there is nothing else to
/// run.
_gnu_inline _gnu_hot
void _gt_io_poll(const int block) {
  if (!_gt_R.io.ok || _gt_R.inflight == 0) { return; }
  if (block) { _gt_io_enter(0, 1, IORING_ENTER_GETEVENTS); }

  unsigned head = *_gt_R.io.cq_head;
  const unsigned tail = __atomic_load_n(_gt_R.io.cq_tail, __ATOMIC_ACQUIRE);
  while (head != tail) {
    struct io_uring_cqe *const cqe = &_gt_R.io.cqes[head & *_gt_R.io.cq_mask];
    gt_task *const t = (gt_task*)(uintptr_t)cqe->user_data;
    if (t != NULL) {
      t->io_res = cqe->res;
      _gt_rq_push(t);
    }
    _gt_R.inflight--;
    ++head;
  }
  __atomic_store_n(_gt_R.io.cq_head, head, __ATOMIC_RELEASE);
}

/// Wait on the ring for at least one completion, no later than an
/// absolute deadline on "clock". The timeout rides in the extended
/// argument, which is relative and measured on the monotonic clock -
/// close enough for a deadline read on another, since the caller
/// checks the deadline again.
_gnu_inline _gnu_nonnull(2)
void _gt_io_poll_until(const clockid_t clock, struct timespec const *deadline) {
  const struct timespec left = _gt_remaining(clock, deadline);
  struct __kernel_timespec ts = {left.tv_sec, left.tv_nsec};
  struct io_uring_getevents_arg arg;
  memset(&arg, 0, sizeof arg);
  arg.ts = (uint64_t)(uintptr_t)&ts;
  syscall(__NR_io_uring_enter, _gt_R.io.fd, 0, 1, IORING_ENTER_GETEVENTS | IORING_ENTER_EXT_ARG, &arg, sizeof arg);
  _gt_io_poll(0);
}

// ==================== I/O OPERATIONS ====================

_gnu_inline _gnu_hot
int _gt_io_ready(void) {
  return _gt_live() && _gt_R.io.ok;
}

/// Submit the entry the current task is waiting on, and
/// suspend until it completes. The task pointer is the
/// completion's user data, which is safe because a parked
/// task cannot be retired: its slot is only handed back by
/// an await, and an await cannot run while the task is
/// blocked here.
_gnu_inline _gnu_hot _gnu_nonnull(1)
int32_t _gt_io_wait(struct io_uring_sqe *sqe) {
  gt_task *const self = _gt_R.current;
  sqe->user_data = (uint64_t)(uintptr_t)self;
  _gt_R.inflight++;

  if (_sppc_unlikely(_gt_io_enter(1, 0, 0) < 0)) {
    _gt_R.inflight--;
    return -errno;
  }

  _gt_block(GT_STATE_WAITING);
  return self->io_res;
}

/// Turn a completion result into what the syscall it stands
/// in for would have returned: io_uring reports failure as
/// a negative errno rather than through errno itself.
#define _gt_io_finish(res_out, r)              \
  do {                                         \
    const int32_t r_ = (r);                    \
    if (r_ < 0) { errno = -r_; *(res_out) = -1; }  \
    else { *(res_out) = r_; }                  \
    return 1;                                  \
  } while (0)

#define _gt_io_begin(sqe_var)                  \
  if (!_gt_io_ready()) { return 0; }           \
  struct io_uring_sqe *const sqe_var = _gt_sqe();              \
  if (sqe_var == NULL) { return 0; }

_gnu_inline _gnu_hot _gnu_nonnull(4)
int _gt_try_read(const int fd, void *buf, const size_t n, ssize_t *res) {
  _gt_io_begin(sqe)
  sqe->opcode = IORING_OP_READ;
  sqe->fd = fd;
  sqe->addr = (uint64_t)(uintptr_t)buf;
  sqe->len = (unsigned)n;
  sqe->off = (uint64_t)-1; // keep using the file's own offset
  _gt_io_finish(res, _gt_io_wait(sqe));
}

_gnu_inline _gnu_hot _gnu_nonnull(4)
int _gt_try_write(const int fd, void const *buf, const size_t n, ssize_t *res) {
  _gt_io_begin(sqe)
  sqe->opcode = IORING_OP_WRITE;
  sqe->fd = fd;
  sqe->addr = (uint64_t)(uintptr_t)buf;
  sqe->len = (unsigned)n;
  sqe->off = (uint64_t)-1;
  _gt_io_finish(res, _gt_io_wait(sqe));
}

_gnu_inline _gnu_hot _gnu_nonnull(5)
int _gt_try_recv(const int fd, void *buf, const size_t n, const int flags, ssize_t *res) {
  _gt_io_begin(sqe)
  sqe->opcode = IORING_OP_RECV;
  sqe->fd = fd;
  sqe->addr = (uint64_t)(uintptr_t)buf;
  sqe->len = (unsigned)n;
  sqe->msg_flags = (unsigned)flags;
  _gt_io_finish(res, _gt_io_wait(sqe));
}

_gnu_inline _gnu_hot _gnu_nonnull(5)
int _gt_try_send(const int fd, void const *buf, const size_t n, const int flags, ssize_t *res) {
  _gt_io_begin(sqe)
  sqe->opcode = IORING_OP_SEND;
  sqe->fd = fd;
  sqe->addr = (uint64_t)(uintptr_t)buf;
  sqe->len = (unsigned)n;
  sqe->msg_flags = (unsigned)flags;
  _gt_io_finish(res, _gt_io_wait(sqe));
}

_gnu_inline _gnu_hot _gnu_nonnull(4)
int _gt_try_accept(const int fd, struct sockaddr *addr, socklen_t *addrlen, int *res) {
  _gt_io_begin(sqe)
  sqe->opcode = IORING_OP_ACCEPT;
  sqe->fd = fd;
  sqe->addr = (uint64_t)(uintptr_t)addr;
  sqe->off = (uint64_t)(uintptr_t)addrlen;
  sqe->accept_flags = SOCK_CLOEXEC;
  _gt_io_finish(res, _gt_io_wait(sqe));
}

_gnu_inline _gnu_hot _gnu_nonnull(2, 4)
int _gt_try_connect(const int fd, struct sockaddr const *addr, const socklen_t addrlen, int *res) {
  _gt_io_begin(sqe)
  sqe->opcode = IORING_OP_CONNECT;
  sqe->fd = fd;
  sqe->addr = (uint64_t)(uintptr_t)addr;
  sqe->off = (uint64_t)addrlen;
  _gt_io_finish(res, _gt_io_wait(sqe));
}

_gnu_inline _gnu_nonnull(2)
int _gt_try_fsync(const int fd, int *res) {
  _gt_io_begin(sqe)
  sqe->opcode = IORING_OP_FSYNC;
  sqe->fd = fd;
  _gt_io_finish(res, _gt_io_wait(sqe));
}

/// Sleeping is the one case where the plain call is NOT
/// the right fallback on a task: it would stop every other
/// task for the duration. An absolute deadline and the clock
/// it is read from carry over as timeout flags. A clock the
/// ring cannot time against falls back to the plain call -
/// checked before taking a submission entry, which could not
/// be handed back. A timeout expiring normally reports -ETIME,
/// which is success here.
_gnu_inline _gnu_nonnull(3, 4)
int _gt_try_sleep(const clockid_t clock, const int flags, struct timespec const *duration, int *res) {
  if (clock != CLOCK_MONOTONIC && clock != CLOCK_REALTIME && clock != CLOCK_BOOTTIME) { return 0; }
  _gt_io_begin(sqe)
  gt_task *const self = _gt_R.current;
  self->io.ts.tv_sec = duration->tv_sec;
  self->io.ts.tv_nsec = duration->tv_nsec;

  sqe->opcode = IORING_OP_TIMEOUT;
  sqe->addr = (uint64_t)(uintptr_t)&self->io.ts;
  sqe->len = 1;
  sqe->off = 0;
  sqe->timeout_flags =
    ((flags & TIMER_ABSTIME) ? IORING_TIMEOUT_ABS : 0u) |
    (clock == CLOCK_REALTIME ? IORING_TIMEOUT_REALTIME : clock == CLOCK_BOOTTIME ? IORING_TIMEOUT_BOOTTIME : 0u);

  const int32_t r = _gt_io_wait(sqe);
  if (r == -ETIME || r == 0) {
    *res = 0;
    return 1;
  }
  _gt_io_finish(res, r);
}
