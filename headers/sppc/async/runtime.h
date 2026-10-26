#pragma once

/// The runtime's types and its thread-local state, shared by every part of
/// it: the scheduler, the stack and context code, and the i/o backend.

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <sppc/config.h>

/// The i/o backend, picked per platform. io_uring on a Linux kernel (where
/// setting it up can still fail at run time, and every call then falls back
/// to its plain syscall); the blocking stand-in everywhere else for now.
/// Defining SPPC_ASYNC_BLOCKING forces the stand-in, to test without a ring.
#if !defined(SPPC_ASYNC_BLOCKING) && SPPC_KERNEL_LINUX
  #define SPPC_ASYNC_IO_URING 1
#else
  #define SPPC_ASYNC_IO_URING 0
#endif

#include <sppc/macros.h>
#include <sppc/closure.h>
#include <sppc/types.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>

// "MAP_NORESERVE" keeps Linux from reserving swap for a mapping
// only part of which is ever touched. FreeBSD has dropped the
// flag: it never reserves for anonymous memory to begin with.
#if defined(MAP_NORESERVE)
  #define _SPPC_MAP_NORESERVE MAP_NORESERVE
#else
  #define _SPPC_MAP_NORESERVE 0
#endif

#define GT_IDX_BITS 20u
#define GT_IDX_MASK ((1u << GT_IDX_BITS) - 1u)
#define GT_GEN_MASK 0x7FFu
#define GT_MAP_FLAGS (MAP_PRIVATE | MAP_ANONYMOUS | _SPPC_MAP_NORESERVE)
#define GT_STACK_SIZE (128u * 1024u)
#define GT_STACK_MIN (16u * 1024u)
#define GT_STACK_CACHE 32u

// ==================== I/O BACKEND STATE ====================

#if SPPC_ASYNC_IO_URING
#include <linux/io_uring.h>

/// The submission and completion rings, mapped out of the kernel.
/// Only the pieces actually used are kept: this is not a general
/// io_uring binding, it's the one queue the runtime parks tasks on.
typedef struct gt_io {
  int fd;
  int ok;
  unsigned *sq_head, *sq_tail, *sq_mask, *sq_array;
  struct io_uring_sqe *sqes;
  unsigned *cq_head, *cq_tail, *cq_mask;
  struct io_uring_cqe *cqes;
  void *sq_mmap, *cq_mmap;
  size_t sq_bytes, cq_bytes, sqe_bytes;
  unsigned sq_entries;
} gt_io;

typedef struct gt_io_task {
  struct __kernel_timespec ts;
} gt_io_task;
#else
/// No backend: nothing is kept, as nothing is ever outstanding.
typedef struct gt_io { char unused; } gt_io;
typedef struct gt_io_task { char unused; } gt_io_task;
#endif

// ==================== TYPES ====================

typedef int gt_handle_t;

enum gt_state_t {
  GT_STATE_FREE = 0,
  GT_STATE_READY,
  GT_STATE_RUNNING,
  GT_STATE_JOINING,
  GT_STATE_WAITING,
  GT_STATE_DONE,
};

/// The main task struct used in the asynchronous green-thread
/// runtime. It contains safe/unsafe stack info, the function
/// being run, and meta-data for safe controlling.
typedef struct gt_task {
  // The stack pointer this task was last switched away from,
  // and the mapping it points into. The main task borrows the
  // thread's own stack and so has no mapping of its own, which
  // is also how "on a task" is told apart from "on the thread
  // that owns the runtime".
  void *sp;
  void *stack;
  size_t stack_size;

  // The unsafe half of a split stack, and this task's own
  // pointer into it. Every function the compiler split keeps
  // its overflowable objects here rather than next to a return
  // address, and the pointer is thread-local - so tasks sharing
  // one os thread share one pointer unless each carries its own
  // across a switch. Without this two tasks interleave on one
  // unsafe stack, and a task that resumes and returns unwinds
  // it past frames another task is still using.
  void *ustack;
  size_t ustack_size;
  void *usp;

  struct gt_task *next;
  struct gt_task *waiters;
  struct gt_task *wnext;

  // What the task runs, and the environment it captured. Taking
  // a closure rather than a function and an argument list is what
  // lets an s++ callable cross the boundary intact: captures ride
  // along in "env", and the result is written to a cell the caller
  // allocated and the closure captured, so this side never has to
  // know the size or layout of an s++ type.
  sppc_closure body;

  // How many tasks are parked on this one's waiter list. The slot
  // can only be retired once, so the awaiter that leaves last is
  // the one that does it - otherwise every awaiter of the same
  // task pushes the slot onto the free list again and the list
  // becomes a cycle.
  uint32_t njoiners;

  // What the i/o backend needs to keep per task for as long as an
  // operation is outstanding - an io_uring timeout's deadline is read
  // by the kernel after the submission returns, so it cannot live on
  // the caller's stack.
  gt_io_task io;

  int32_t io_res;
  uint32_t idx;
  uint32_t gen;
  uint8_t state;
  uint8_t detached;
} gt_task;

/// A task waiting on a thread lock held by another task on the
/// same thread. It lives on the waiting task's own stack, for
/// exactly as long as the wait.
typedef struct gt_lock_waiter {
  void const *lock;
  gt_task *task;
  struct gt_lock_waiter *next;
} gt_lock_waiter;

/// The single thread-local asynchronous runtime that handles all
/// the task management, stacks, switching etc.
typedef struct gt_runtime {
  gt_task *current;
  gt_task *rq_head, *rq_tail;
  gt_task *free_slots;
  gt_task *zombies;
  gt_task **slots;
  uint32_t nslots, cslots;
  size_t live;
  size_t stack_size;
  size_t page;
  int guard;
  void *stack_cache[GT_STACK_CACHE];
  unsigned stack_cached;
  size_t cached_size;

  // The i/o backend's own state, and how many operations are outstanding
  // with it: tasks parked there that will become runnable again.
  gt_io io;
  unsigned inflight;

  // Tasks waiting on a thread lock, oldest first, so an unlock
  // can hand the lock to the one that has waited longest.
  gt_lock_waiter *lock_waiters;
} gt_runtime;

/// The runtime is reached through a thread-local pointer rather than
/// being a thread-local itself. A dlopen'd library's own tls is dynamic,
/// so a thread-local struct costs a "__tls_get_addr" call in every
/// function that touches it; an 8-byte pointer fits the static tls
/// reserve, where initial-exec makes each access one load off the thread
/// pointer. Null until the runtime starts on this thread, which is also
/// how "has it started" is asked. The block is freed at thread exit.
extern _sppc_tls_initial_exec _sppc_thread_local gt_runtime *_gt_rt;
#define _gt_R (*_gt_rt)
#define _gt_live() (_gt_rt != NULL)

extern pthread_key_t _gt_rt_key;
extern pthread_once_t _gt_rt_key_once;
extern _sppc_thread_local void *__safestack_unsafe_stack_ptr;
extern bool _unsafe_stack_wanted;

inline void _gt_init(void);
inline void _gt_rt_free(void *rt);
// The i/o backend's side of the scheduler, defined by async/io_uring.h or
// async/blocking.h: start and stop it, collect finished operations (waiting
// for at least one if "block" is set), and wait for one no later than an
// absolute deadline.
inline void _gt_io_setup(void);
inline void _gt_io_teardown(void);
inline void _gt_io_poll(int block);
inline void _gt_io_poll_until(clockid_t clock, struct timespec const *deadline);

_gnu_noreturn _gnu_cold _gnu_nonnull(1) _gnu_format_printf(1, 2)
_gnu_inline_va void _gt_panic(const char *msg, ...) {
  va_list ap;
  va_start(ap, msg);
  fputs("[sppc] [CRITICAL] ", stderr);
  vfprintf(stderr, msg, ap);
  fputc('\n', stderr);
  va_end(ap);
  abort();
}

/// The "handle" is a task's index and generation packed into
/// one int. The generation allows the handles to be recycled,
/// without breaking identification of old tasks etc.
_gnu_inline _gnu_hot _gnu_nonnull(1)
gt_handle_t _gt_handle(gt_task const *t) {
  return (gt_handle_t)((t->gen << GT_IDX_BITS) | t->idx);
}
