/// A green-thread runtime: many tasks multiplexed onto one
/// os thread, each with a stack of its own, switched
/// cooperatively. A task suspends only where the runtime
/// says it may - at a yield, at an await, or inside an i/o
/// call that would otherwise have blocked - so nothing here
/// needs a lock: the whole runtime is thread-local and only
/// one task of it runs at a time.
///
/// Everything a task needs to be resumed lives on its own
/// stack, pushed there by "_gt_switch", so a task is just
/// a stack pointer plus the bookkeeping below. That is what
/// makes a fresh task cheap: lay out the register block a
/// switch would have pushed and the first entry is
/// indistinguishable from a resume.

#pragma once
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <sppc/closure.h>
#include <sppc/macros.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/io_uring.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <unistd.h>

#define GT_IDX_BITS 20u
#define GT_IDX_MASK ((1u << GT_IDX_BITS) - 1u)
#define GT_GEN_MASK 0x7FFu
#define GT_MAP_FLAGS (MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE)
#define GT_STACK_SIZE (128u * 1024u)
#define GT_STACK_MIN (16u * 1024u)
#define GT_STACK_CACHE 32u

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

  // A timeout's deadline is read by the kernel after the submission
  // returns, so it cannot live on the caller's stack.
  struct __kernel_timespec ts;

  int32_t io_res;
  uint32_t idx;
  uint32_t gen;
  uint8_t state;
  uint8_t detached;
} gt_task;

/// The submission and completion rings, mapped out of the kernel.
/// Only the pieces actually used are kept: this is not a general
/// io_uring binding, it's the one queue the runtime parks tasks on.
typedef struct gt_ring {
  int fd;
  int ok;
  unsigned *sq_head, *sq_tail, *sq_mask, *sq_array;
  struct io_uring_sqe *sqes;
  unsigned *cq_head, *cq_tail, *cq_mask;
  struct io_uring_cqe *cqes;
  void *sq_mmap, *cq_mmap;
  size_t sq_bytes, cq_bytes, sqe_bytes;
  unsigned sq_entries;
  unsigned inflight;
} gt_ring;

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
  int inited;
  gt_ring ring;
} gt_runtime;

extern _Thread_local gt_runtime _gt_R;
extern _Thread_local void *__safestack_unsafe_stack_ptr;
extern bool _unsafe_stack_wanted;

extern void _gt_switch(void **save_sp, void *const *load_sp);

_gnu_noreturn _gnu_cold _gnu_nonnull(1)
_gnu_inline_va void _gt_panic(const char *msg, ...) {
  va_list ap;
  __builtin_va_start(ap, msg);
  fputs("[sppc] [CRITICAL] ", stderr);
  vfprintf(stderr, msg, ap);
  fputc('\n', stderr);
  __builtin_va_end(ap);
  abort();
}

/// The "handle" is a task's index and generation packed into
/// one int. The generation allows the handles to be recycled,
/// without breaking identification of old tasks etc.
_gnu_inline _gnu_hot _gnu_nonnull(1)
gt_handle_t _gt_handle(gt_task const *t) {
  return (gt_handle_t)((t->gen << GT_IDX_BITS) | t->idx);
}

_gnu_inline
size_t _gt_page(void) {
  if (_sppc_unlikely(!_gt_R.page)) { _gt_R.page = (size_t)sysconf(_SC_PAGESIZE); }
  return _gt_R.page;
}

// ==================== STACK ====================

_gnu_inline
void* _gt_stack_alloc(const size_t size) {
  if (_gt_R.stack_cached && _gt_R.cached_size == size) {
    return _gt_R.stack_cache[--_gt_R.stack_cached];
  }

  const auto pg = _gt_page();
  if (!_gt_R.guard) {
    const auto p = mmap(NULL, size, PROT_READ | PROT_WRITE, GT_MAP_FLAGS, -1, 0);
    return p == MAP_FAILED ? NULL : p;
  }

  // The guard goes at the low end, the end a stack grows towards,
  // so running off the bottom of one lands on nothing mapped
  // rather than on the task whose stack the allocator happened to
  // put below it. The usable region therefore starts one page in,
  // and that - not the mapping - is what the caller is given.
  const auto total = size + pg;
  const auto p = mmap(NULL, total, PROT_NONE, GT_MAP_FLAGS, -1, 0);
  if (p == MAP_FAILED) { return NULL; }
  if (mprotect((char*)p + pg, size, PROT_READ | PROT_WRITE) != 0) {
    munmap(p, total);
    return NULL;
  }
  return (char*)p + pg;
}

_gnu_inline
void _gt_stack_free(void *usable, const size_t size) {
  if (usable == NULL) { return; }
  if (_gt_R.stack_cached < GT_STACK_CACHE &&
    (_gt_R.stack_cached == 0 || _gt_R.cached_size == size)) {
    _gt_R.cached_size = size;
    _gt_R.stack_cache[_gt_R.stack_cached++] = usable;
    return;
  }
  const auto pg = _gt_page();
  if (_gt_R.guard) { munmap((char*)usable - pg, size + pg); }
  else { munmap(usable, size); }
}

/// Lay out the stack of the new task, in such a way that the
/// first switch into it looks like a "resume" - the register
/// block "_gt_switch" would have pushed, and below the top of
/// the stack the address it will "ret" to.
_gnu_inline _gnu_nonnull(1, 3)
void* _gt_stack_prime(void *usable, const size_t size, void (*entry)(void)) {
  auto top = (uintptr_t)usable + size;
  top &= ~(uintptr_t)15;

#if defined(__x86_64__)
  // From low to high the switch leaves: the sse/x87 control word
  // slot, r15, r14, r13, r12, rbx, rbp, and then the return address.
  // The return address must sit on a 16-byte boundary, because "ret"
  // pops it and SysV wants rsp % 16 == 8 once the callee is entered.
  const auto ret_slot = top - 16;
  const auto base = ret_slot - 56;
  memset((void*)base, 0, 56);
  *(uint32_t*)base = 0x1F80u; // mxcsr: all exceptions masked, round to nearest
  *(uint16_t*)(base + 4) = 0x037Fu; // x87 control word: the abi's default
  *(uint64_t*)ret_slot = (uint64_t)(uintptr_t)entry;
  return (void*)base;
#elif defined(__aarch64__)
  // x19-x28, x29, x30, d8-d15 and fpcr, in the order "_gt_switch"
  // stores them. x30 is what "ret" branches to, so the entry point
  // goes there.
  const auto base = top - 176;
  memset((void*)base, 0, 176);
  *(uint64_t*)(base + 88) = (uint64_t)(uintptr_t)entry;
  return (void*)base;
#else
#  error "the green-thread runtime cannot lay out a stack for this architecture"
#endif
}

_gnu_inline
void _gt_set_guard_pages(const int enabled) {
  if (!!enabled == !!_gt_R.guard) { return; }
  const auto pg = _gt_page();
  while (_gt_R.stack_cached) {
    const auto p = _gt_R.stack_cache[--_gt_R.stack_cached];
    if (_gt_R.guard) { munmap((char*)p - pg, _gt_R.cached_size + pg); }
    else { munmap(p, _gt_R.cached_size); }
  }
  _gt_R.guard = !!enabled;
}

// ==================== SCHEDULE ====================

/// Push the new task into the runtime environment, by linking
/// the new task as the "next" of the current "tail" task (if
/// there *is* a tail talk at the moment), otherwise as the
/// head of the list. Either way, this task also registers as
/// the new tail of the list.
_gnu_inline _gnu_hot _gnu_nonnull(1)
void _gt_rq_push(gt_task *t) {
  t->state = GT_STATE_READY;
  t->next = NULL;
  if (_gt_R.rq_tail) { _gt_R.rq_tail->next = t; }
  else { _gt_R.rq_head = t; }
  _gt_R.rq_tail = t;
}

/// Pop a task off of the runtime list by taking the head of the
/// list, and setting the head to the old head tasks's "next"
/// task. Update the runtime's head/tail task information based
/// on the existence of other tasks.
_gnu_inline _gnu_hot
gt_task* _gt_rq_pop(void) {
  const auto t = _gt_R.rq_head;
  if (!t) { return NULL; }
  _gt_R.rq_head = t->next;
  if (!_gt_R.rq_head) { _gt_R.rq_tail = NULL; }
  t->next = NULL;
  return t;
}

// ==================== I/O ====================

/// Raw syscalls rather than liburing: the runtime needs
/// six opcodes and one queue, and taking a dependency for
/// that would put a library between the scheduler and the
/// only thing it blocks on.
#define GT_RING_ENTRIES 256u

/// Wrapper to enter the io_uring task and pull the result
/// out through the return int of the syscall.
_gnu_inline
int _gt_io_enter(const unsigned to_submit, const unsigned min_complete, const unsigned flags) {
  return (int)syscall(__NR_io_uring_enter, _gt_R.ring.fd, to_submit, min_complete, flags, NULL, 0);
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
  const auto fd = (int)syscall(__NR_io_uring_setup, GT_RING_ENTRIES, &params);
  if (fd < 0) {
    _gt_R.ring.ok = 0;
    return;
  }

  // The "CQ" bytes are the completion ring, the "SQ" bytes are
  // the submission ring. These are used by the ring for normal
  // operations (after mmap).
  auto sq_bytes = (size_t)params.sq_off.array + params.sq_entries * sizeof(unsigned);
  auto cq_bytes = (size_t)params.cq_off.cqes + params.cq_entries * sizeof(struct io_uring_cqe);
  const auto single = (params.features & IORING_FEAT_SINGLE_MMAP) != 0;
  if (single) {
    if (cq_bytes > sq_bytes) { sq_bytes = cq_bytes; }
    cq_bytes = sq_bytes;
  }

  // Build the sequential ring from the SQ bytes, checking for
  // allocation failures.
  const auto sq = mmap(
    NULL, sq_bytes, PROT_READ | PROT_WRITE,
    MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQ_RING);
  if (sq == MAP_FAILED) {
    close(fd);
    _gt_R.ring.ok = 0;
    return;
  }

  // Assuming a single mapping, default the completion ring to
  // sequential ring, and then do the manual re-mapping if we
  // are otherwise.
  auto cq = sq;
  if (!single) {
    cq = mmap(
      NULL, cq_bytes, PROT_READ | PROT_WRITE,
      MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_CQ_RING);
    if (cq == MAP_FAILED) {
      munmap(sq, sq_bytes);
      close(fd);
      _gt_R.ring.ok = 0;
      return;
    }
  }

  // Finally, the SQE (sequential ring entries), whose byte
  // size is the number of SQ entries and the additional struct
  // size.
  const auto sqe_bytes = params.sq_entries * sizeof(struct io_uring_sqe);
  const auto sqes = mmap(
    NULL, sqe_bytes, PROT_READ | PROT_WRITE,
    MAP_SHARED | MAP_POPULATE, fd, IORING_OFF_SQES);
  if (sqes == MAP_FAILED) {
    if (!single) { munmap(cq, cq_bytes); }
    munmap(sq, sq_bytes);
    close(fd);
    _gt_R.ring.ok = 0;
    return;
  }

  // Set up the entire ring, with all the SQ/CQ/SQE data, and
  // offsets for the pointers, ie pointing to sections within
  // the SQ/CQ data.
  _gt_R.ring.fd = fd;
  _gt_R.ring.sq_mmap = sq;
  _gt_R.ring.cq_mmap = single ? NULL : cq;
  _gt_R.ring.sq_bytes = sq_bytes;
  _gt_R.ring.cq_bytes = cq_bytes;
  _gt_R.ring.sqe_bytes = sqe_bytes;
  _gt_R.ring.sq_entries = params.sq_entries;
  _gt_R.ring.sqes = (struct io_uring_sqe*)sqes;
  _gt_R.ring.sq_head = (unsigned*)((char*)sq + params.sq_off.head);
  _gt_R.ring.sq_tail = (unsigned*)((char*)sq + params.sq_off.tail);
  _gt_R.ring.sq_mask = (unsigned*)((char*)sq + params.sq_off.ring_mask);
  _gt_R.ring.sq_array = (unsigned*)((char*)sq + params.sq_off.array);
  _gt_R.ring.cq_head = (unsigned*)((char*)cq + params.cq_off.head);
  _gt_R.ring.cq_tail = (unsigned*)((char*)cq + params.cq_off.tail);
  _gt_R.ring.cq_mask = (unsigned*)((char*)cq + params.cq_off.ring_mask);
  _gt_R.ring.cqes = (struct io_uring_cqe*)((char*)cq + params.cq_off.cqes);
  _gt_R.ring.inflight = 0;
  _gt_R.ring.ok = 1;
}

/// Tear down the green thread environment, unmapping the
/// SQ/CQ memory for the runtime, and cleaning up the io_uring
/// metadata too.
_gnu_inline _gnu_cold
void _gt_io_teardown(void) {
  if (!_gt_R.ring.ok) { return; }
  munmap(_gt_R.ring.sqes, _gt_R.ring.sqe_bytes);
  if (_gt_R.ring.cq_mmap) { munmap(_gt_R.ring.cq_mmap, _gt_R.ring.cq_bytes); }
  munmap(_gt_R.ring.sq_mmap, _gt_R.ring.sq_bytes);
  close(_gt_R.ring.fd);
  memset(&_gt_R.ring, 0, sizeof _gt_R.ring);
}

/// Claim a submission slot. The runtime is single-threaded, so
/// the only writer of the tail is this code and the only reader
/// that matters is the kernel - hence the release store, which
/// is what publishes the entry to it.
_gnu_inline _gnu_hot
struct io_uring_sqe* _gt_sqe(void) {
  // Get the tail and head, and do a validity check that the
  // entries are set up correctly.
  const auto tail = *_gt_R.ring.sq_tail;
  const auto head = __atomic_load_n(_gt_R.ring.sq_head, __ATOMIC_ACQUIRE);
  if (_sppc_unlikely(tail - head >= _gt_R.ring.sq_entries)) { return NULL; }

  // Todo: Document this section.
  const auto idx = tail & *_gt_R.ring.sq_mask;
  const auto sqe = &_gt_R.ring.sqes[idx];
  memset(sqe, 0, sizeof *sqe);
  _gt_R.ring.sq_array[idx] = idx;
  __atomic_store_n(_gt_R.ring.sq_tail, tail + 1, __ATOMIC_RELEASE);
  return sqe;
}

/// Collect finished operations and make their tasks runnable
/// again. With "block" set this waits for at least one, which
/// is what the scheduler does when there is nothing else to
/// run.
_gnu_inline _gnu_hot
void _gt_io_poll(const int block) {
  if (!_gt_R.ring.ok || _gt_R.ring.inflight == 0) { return; }
  if (block) { _gt_io_enter(0, 1, IORING_ENTER_GETEVENTS); }

  auto head = *_gt_R.ring.cq_head;
  const auto tail = __atomic_load_n(_gt_R.ring.cq_tail, __ATOMIC_ACQUIRE);
  while (head != tail) {
    const auto cqe = &_gt_R.ring.cqes[head & *_gt_R.ring.cq_mask];
    const auto t = (gt_task*)(uintptr_t)cqe->user_data;
    if (t != NULL) {
      t->io_res = cqe->res;
      _gt_rq_push(t);
    }
    _gt_R.ring.inflight--;
    ++head;
  }
  __atomic_store_n(_gt_R.ring.cq_head, head, __ATOMIC_RELEASE);
}

/// Release the stacks of tasks that have finished. A task
/// cannot unmap the stack it is still running on, so it
/// parks itself here on the way out and whatever runs next
/// does it. The slot itself outlives the stack unless the
/// task was detached, because an await still has to be able
/// to resolve the handle and see that it is done.
_gnu_inline
void _gt_reap(void) {
  auto z = _gt_R.zombies;
  _gt_R.zombies = NULL;
  while (z) {
    const auto next = z->next;
    z->next = NULL;
    if (z->stack) {
      _gt_stack_free(z->stack, z->stack_size);
      z->stack = NULL;
    }
    if (z->ustack) {
      _gt_stack_free(z->ustack, z->ustack_size);
      z->ustack = NULL;
    }
    if (z->detached) {
      z->state = GT_STATE_FREE;
      z->gen = (z->gen + 1) & GT_GEN_MASK;
      if (z->gen == 0) { z->gen = 1; }
      z->next = _gt_R.free_slots;
      _gt_R.free_slots = z;
      _gt_R.live--;
    }
    z = next;
  }
}

_gnu_inline _gnu_hot _gnu_nonnull(1)
void _gt_switch_to(gt_task *next) {
  const auto self = _gt_R.current;
  if (next == self) {
    self->state = GT_STATE_RUNNING;
    return;
  }
  _gt_R.current = next;
  next->state = GT_STATE_RUNNING;

  // The unsafe stack pointer is one thread-local shared
  // by every task on this os thread, so it has to travel
  // with the switch or the tasks interleave on it. Saved
  // and restored around the switch rather than inside it,
  // because here it is one named variable and in the
  // assembly it would be an open-coded TLS access per
  // architecture.
  self->usp = __safestack_unsafe_stack_ptr;
  __safestack_unsafe_stack_ptr = next->usp;

  _gt_switch(&self->sp, &next->sp);

  // Reached on the way back in, once something switches
  // to "self" again.
  if (_sppc_unlikely(_gt_R.zombies != NULL)) { _gt_reap(); }
}

_gnu_inline
gt_task* _gt_pick(void) {
  for (;;) {
    if (_gt_R.ring.inflight) { _gt_io_poll(0); }
    const auto t = _gt_rq_pop();
    if (t) { return t; }

    // Nothing is runnable, but something is outstanding,
    // so something will be. Waiting on the ring here is
    // what turns a task blocking on i/o into the thread
    // idling rather than spinning.
    if (_gt_R.ring.inflight) {
      _gt_io_poll(1);
      continue;
    }
    return NULL;
  }
}

/// Suspend the running task in the given state. Whoever
/// is going to make it runnable again must already have
/// arranged to do so (put it on a waiter list, or handed
/// it to the i/o engine) as nothing here will.
_gnu_inline
void _gt_block(const int state) {
  _gt_R.current->state = (uint8_t)state;
  const auto next = _gt_pick();
  if (_sppc_unlikely(!next)) {
    _gt_panic("deadlock: task %u blocked with nothing left to run", _gt_R.current->idx);
  }
  _gt_switch_to(next);
}

// ==================== TASK SLOTS ====================

_gnu_inline
gt_task* _gt_slot_alloc(void) {
  auto t = _gt_R.free_slots;
  if (t) {
    _gt_R.free_slots = t->next;
    const auto idx = t->idx;
    const auto gen = t->gen;
    memset(t, 0, sizeof *t);
    t->idx = idx;
    t->gen = gen ? gen : 1;
  }
  else {
    if (_gt_R.nslots == _gt_R.cslots) {
      const auto nc = _gt_R.cslots ? _gt_R.cslots * 2 : 64;
      if (nc > GT_IDX_MASK) { return NULL; }
      gt_task **s = realloc(_gt_R.slots, nc * sizeof *s);
      if (!s) { return NULL; }
      _gt_R.slots = s;
      _gt_R.cslots = nc;
    }
    t = calloc(1, sizeof *t);
    if (!t) { return NULL; }
    t->idx = _gt_R.nslots;
    t->gen = 1;
    _gt_R.slots[_gt_R.nslots++] = t;
  }
  _gt_R.live++;
  return t;
}

_gnu_inline _gnu_nonnull(1)
void _gt_slot_free(gt_task *t) {
  t->state = GT_STATE_FREE;
  t->gen = (t->gen + 1) & GT_GEN_MASK;
  if (t->gen == 0) { t->gen = 1; }
  t->next = _gt_R.free_slots;
  _gt_R.free_slots = t;
  _gt_R.live--;
}

_gnu_inline _gnu_hot
gt_task* _gt_resolve(const gt_handle_t handle) {
  const auto idx = (uint32_t)handle & GT_IDX_MASK;
  const auto gen = ((uint32_t)handle >> GT_IDX_BITS) & GT_GEN_MASK;
  if (_sppc_unlikely(handle <= 0 || idx >= _gt_R.nslots)) { return NULL; }
  const auto t = _gt_R.slots[idx];
  if (_sppc_unlikely(t->gen != gen || t->state == GT_STATE_FREE)) { return NULL; }
  return t;
}

// ==================== INIT ====================

_gnu_inline _gnu_cold
void _gt_init(void) {
  if (_sppc_likely(_gt_R.inited)) { return; }
  _gt_R.inited = 1;
  _gt_R.stack_size = GT_STACK_SIZE;
  _gt_R.guard = 1;

  // The thread that starts the runtime becomes a task, so
  // that switching is uniform: there is no separate "main
  // context" to special-case, and the scheduler picks the
  // thread's own task like any other. It borrows the thread's
  // stack, which is what a null "stack" means here.
  const auto main_task = _gt_slot_alloc();
  if (!main_task) { _gt_panic("cannot start the green-thread runtime"); }
  main_task->state = GT_STATE_RUNNING;
  main_task->stack = NULL;
  main_task->usp = __safestack_unsafe_stack_ptr;
  _gt_R.current = main_task;
  _gt_io_setup();
}

_gnu_inline
void _gt_set_stack_size(size_t bytes) {
  const auto pg = _gt_page();
  if (bytes < GT_STACK_MIN) { bytes = GT_STACK_MIN; }
  _gt_R.stack_size = (bytes + pg - 1u) & ~(pg - 1u);
}

// ==================== SPAWN AND AWAIT ====================

/// Where a task begins and ends. It is reached by the "ret"
/// at the end of the first switch into the task, not by a
/// call, so it has no caller to return to and must hand
/// control on itself.
_gnu_inline_va
void _gt_trampoline(void) {
  const auto self = _gt_R.current;

  // Reached with the previous task's stack already released
  // if it had finished; a fresh task is the one place a switch
  // lands outside "_gt_switch_to", so it repeats what that
  // does on the way back in.
  if (_sppc_unlikely(_gt_R.zombies != NULL)) { _gt_reap(); }

  self->body.fn(self->body.env);
  free(self->body.env);
  self->body.env = NULL;
  self->state = GT_STATE_DONE;

  // Wake everything that was waiting on this task, not just the
  // first: a future can be awaited from more than one place, and
  // a single waiter slot silently loses all but the last of them.
  auto w = self->waiters;
  self->waiters = NULL;
  while (w) {
    const auto n = w->wnext;
    w->wnext = NULL;
    _gt_rq_push(w);
    w = n;
  }

  // The stack under our feet cannot be unmapped from here,
  // so hand it over and let whatever runs next do it.
  self->next = _gt_R.zombies;
  _gt_R.zombies = self;

  const auto next = _gt_pick();
  if (_sppc_unlikely(!next)) {
    // Every task that could still run has finished, so
    // control belongs back on the thread's own task - which
    // is only unreachable here if it was never registered,
    // and that cannot happen after "_gt_init".
    _gt_panic("task %u finished with nothing left to run", self->idx);
  }

  _gt_R.current = next;
  next->state = GT_STATE_RUNNING;
  __safestack_unsafe_stack_ptr = next->usp;
  _gt_switch(&self->sp, &next->sp);
  _gt_panic("a finished task was resumed");
}

/// Allocate a new slot for the task, and a stack for the slot
/// (and an unsafe stack if they are in use). Then set all
/// the metadata on the task and inject it into the runtime.
_gnu_inline _gnu_restrict_access(write_only, 1) _gnu_nonnull(1)
int _gt_spawn(gt_handle_t *out, const sppc_closure body, void (*entry)(void)) {
  _gt_init();

  // Allocate a new task from the runtime, either from the
  // free slot list or by allocating new slots.
  const auto t = _gt_slot_alloc();
  if (!t) { return ENOMEM; }

  // Allocate a stack for this new task, based off the size
  // that the runtime has configured. Failing to acquire a
  // stack frees the slot too.
  const auto size = _gt_R.stack_size;
  const auto stack = _gt_stack_alloc(size);
  if (!stack) {
    _gt_slot_free(t);
    return ENOMEM;
  }

  // An unsafe stack only where the program was built with
  // split stacks. No point wasting an unused allocation if
  // they are not in use.
  void *ustack = NULL;
  if (_unsafe_stack_wanted) {
    ustack = _gt_stack_alloc(size);
    if (!ustack) {
      _gt_stack_free(stack, size);
      _gt_slot_free(t);
      return ENOMEM;
    }
  }

  // Set all the metadata onto the new task, including the
  // closure (body) that will be called. Prime the stack
  // for special switch linking / mapping to.
  t->body = body;
  t->stack = stack;
  t->stack_size = size;
  t->ustack = ustack;
  t->ustack_size = ustack ? size : 0;
  t->usp = ustack ? (char*)ustack + size : NULL;
  t->sp = _gt_stack_prime(stack, size, entry);

  // Push the task into the runtime, linking it against the
  // current tail task. Write the acquired handle back into
  // the "out" pointer.
  _gt_rq_push(t);
  *out = _gt_handle(t);
  return 0;
}

/// Give up the processor to whatever else is ready. A task
/// that never yields and never blocks runs to completion,
/// which is the point of a cooperative runtime, but a long
/// computation can hand control on with this.
_gnu_inline
void _gt_yield(void) {
  if (_sppc_unlikely(!_gt_R.inited)) { return; }
  const auto self = _gt_R.current;
  if (_gt_R.ring.inflight) { _gt_io_poll(0); }

  const auto next = _gt_rq_pop();
  if (!next) { return; }
  _gt_rq_push(self);
  _gt_switch_to(next);
}

/// Wait for a task to finish. The value it produced is not
/// read here as it is already in the cell the caller gave the
/// closure; so this only has to make the wait happen and
/// retire the slot afterwards.
_gnu_inline
int _gt_await(const gt_handle_t handle) {
  if (_sppc_unlikely(!_gt_R.inited)) { return EINVAL; }

  const auto t = _gt_resolve(handle);

  // A handle whose slot has been retired no longer resolves,
  // so awaiting the same task twice is refused rather than
  // answered with whatever now occupies the slot. S++ memory
  // rules prevent this from happening anyway (consuming self).
  if (t == NULL) { return EINVAL; }
  if (t == _gt_R.current) { return EDEADLK; }
  if (t->detached) { return EINVAL; }

  if (t->state != GT_STATE_DONE) {
    const auto self = _gt_R.current;
    self->wnext = t->waiters;
    t->waiters = self;
    t->njoiners++;
    _gt_block(GT_STATE_JOINING);
    t->njoiners--;
  }

  // The stack is already gone, released by whoever ran after
  // the task finished. Only the slot is left to hand back,
  // and only once every awaiter has collected.
  if (t->njoiners == 0) { _gt_slot_free(t); }
  return 0;
}

/// Give up the right to await a task. Its slot goes back
/// the moment it finishes rather than being kept for a result
/// nobody will collect, and a task that has already finished
/// is retired here and now.
_gnu_inline
int _gt_detach(const gt_handle_t handle) {
  if (_sppc_unlikely(!_gt_R.inited)) { return EINVAL; }

  const auto t = _gt_resolve(handle);
  if (t == NULL) { return EINVAL; }
  if (t == _gt_R.current) { return EINVAL; }
  if (t->detached) { return EINVAL; }

  t->detached = 1;
  if (t->state == GT_STATE_DONE) { _gt_slot_free(t); }
  return 0;
}

/// Run every task that is ready, and everything they make
/// ready, until nothing is left. Called on the way out of
/// the process (sppc_cleanup) so that a task spawned and
/// never awaited still finishes, rather than being abandoned
/// with its captured environment unreleased.
_gnu_inline _gnu_cold
void _gt_drain(void) {
  if (!_gt_R.inited) { return; }

  const auto self = _gt_R.current;
  for (;;) {
    if (_gt_R.rq_head == NULL) {
      if (_gt_R.ring.inflight == 0) { break; }
      _gt_io_poll(1);
      continue;
    }
    const auto next = _gt_rq_pop();
    _gt_rq_push(self);
    _gt_switch_to(next);
  }

  if (_gt_R.zombies != NULL) { _gt_reap(); }
  _gt_io_teardown();
}

// ==================== I/O OPERATIONS ====================

_gnu_inline _gnu_hot
int _gt_io_ready(void) {
  return _gt_R.inited && _gt_R.ring.ok;
}

/// Submit the entry the current task is waiting on, and
/// suspend until it completes. The task pointer is the
/// completion's user data, which is safe because a parked
/// task cannot be retired: its slot is only handed back by
/// an await, and an await cannot run while the task is
/// blocked here.
_gnu_inline _gnu_hot _gnu_nonnull(1)
int32_t _gt_io_wait(struct io_uring_sqe *sqe) {
  const auto self = _gt_R.current;
  sqe->user_data = (uint64_t)(uintptr_t)self;
  _gt_R.ring.inflight++;

  if (_sppc_unlikely(_gt_io_enter(1, 0, 0) < 0)) {
    _gt_R.ring.inflight--;
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
  const auto sqe_var = _gt_sqe();              \
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
/// task for the duration. A timeout expiring normally
/// reports -ETIME, which is success here.
_gnu_inline _gnu_nonnull(3)
int _gt_try_sleep(const int64_t secs, const int64_t nanos, int *res) {
  _gt_io_begin(sqe)
  const auto self = _gt_R.current;
  self->ts.tv_sec = secs;
  self->ts.tv_nsec = nanos;

  sqe->opcode = IORING_OP_TIMEOUT;
  sqe->addr = (uint64_t)(uintptr_t)&self->ts;
  sqe->len = 1;
  sqe->off = 0;

  const auto r = _gt_io_wait(sqe);
  if (r == -ETIME || r == 0) {
    *res = 0;
    return 1;
  }
  _gt_io_finish(res, r);
}
