#pragma once

/// The scheduler: the run queue, switching, task slots and handles, spawn,
/// await and detach, and the integration with thread locks that lets a task
/// wait on one without stopping the rest of its thread. Portable apart from
/// the thread locks, which are pthread's; the i/o it parks tasks on is the
/// backend's.

#include <sppc/async/runtime.h>
#include <sppc/async/vmem.h>
#include <sppc/async/context.h>
#include <sppc/platform/pthread_ext.h>
#include <errno.h>
#include <string.h>

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
  gt_task *const t = _gt_R.rq_head;
  if (!t) { return NULL; }
  _gt_R.rq_head = t->next;
  if (!_gt_R.rq_head) { _gt_R.rq_tail = NULL; }
  t->next = NULL;
  return t;
}

// ==================== SWITCHING ====================

/// Release the stacks of tasks that have finished. A task
/// cannot unmap the stack it is still running on, so it
/// parks itself here on the way out and whatever runs next
/// does it. The slot itself outlives the stack unless the
/// task was detached, because an await still has to be able
/// to resolve the handle and see that it is done.
_gnu_inline
void _gt_reap(void) {
  gt_task *z = _gt_R.zombies;
  _gt_R.zombies = NULL;
  while (z) {
    gt_task *const next = z->next;
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
  gt_task *const self = _gt_R.current;
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
    if (_gt_R.inflight) { _gt_io_poll(0); }
    gt_task *const t = _gt_rq_pop();
    if (t) { return t; }

    // Nothing is runnable, but something is outstanding,
    // so something will be. Waiting on the ring here is
    // what turns a task blocking on i/o into the thread
    // idling rather than spinning.
    if (_gt_R.inflight) {
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
  gt_task *const next = _gt_pick();
  if (_sppc_unlikely(!next)) {
    _gt_panic("deadlock: task %u blocked with nothing left to run", _gt_R.current->idx);
  }
  _gt_switch_to(next);
}

// ==================== TASK SLOTS ====================

_gnu_inline
gt_task* _gt_slot_alloc(void) {
  gt_task *t = _gt_R.free_slots;
  if (t) {
    _gt_R.free_slots = t->next;
    const uint32_t idx = t->idx;
    const uint32_t gen = t->gen;
    memset(t, 0, sizeof *t);
    t->idx = idx;
    t->gen = gen ? gen : 1;
  }
  else {
    if (_gt_R.nslots == _gt_R.cslots) {
      const uint32_t nc = _gt_R.cslots ? _gt_R.cslots * 2 : 64;
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
  const uint32_t idx = (uint32_t)handle & GT_IDX_MASK;
  const uint32_t gen = ((uint32_t)handle >> GT_IDX_BITS) & GT_GEN_MASK;
  if (_sppc_unlikely(handle <= 0 || idx >= _gt_R.nslots)) { return NULL; }
  gt_task *const t = _gt_R.slots[idx];
  if (_sppc_unlikely(t->gen != gen || t->state == GT_STATE_FREE)) { return NULL; }
  return t;
}

// ==================== INIT ====================

// The once-per-thread start of the runtime, kept out of line: "_gt_init" is
// inlined into every spawn, and only the check that this has run belongs there.
_gnu_inline_va
void _gt_rt_key_create(void) {
  if (pthread_key_create(&_gt_rt_key, _gt_rt_free) != 0) {
    _gt_panic("cannot register the green-thread runtime's thread-exit hook");
  }
}

_gnu_noinline _gnu_cold
inline void _gt_init_slow(void) {
  gt_runtime *const rt = (gt_runtime*)calloc(1, sizeof *rt);
  if (!rt) { _gt_panic("cannot allocate the green-thread runtime"); }
  pthread_once(&_gt_rt_key_once, _gt_rt_key_create);
  pthread_setspecific(_gt_rt_key, rt);
  _gt_rt = rt;
  _gt_R.stack_size = GT_STACK_SIZE;
  _gt_R.guard = 1;

  // The thread that starts the runtime becomes a task, so
  // that switching is uniform: there is no separate "main
  // context" to special-case, and the scheduler picks the
  // thread's own task like any other. It borrows the thread's
  // stack, which is what a null "stack" means here.
  gt_task *const main_task = _gt_slot_alloc();
  if (!main_task) { _gt_panic("cannot start the green-thread runtime"); }
  main_task->state = GT_STATE_RUNNING;
  main_task->stack = NULL;
  main_task->usp = __safestack_unsafe_stack_ptr;
  _gt_R.current = main_task;
  _gt_io_setup();
}

_gnu_inline _gnu_hot
void _gt_init(void) {
  if (_sppc_unlikely(!_gt_live())) { _gt_init_slow(); }
}

_gnu_inline
void _gt_set_stack_size(size_t bytes) {
  const size_t pg = _gt_page();
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
  gt_task *const self = _gt_R.current;

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
  gt_task *w = self->waiters;
  self->waiters = NULL;
  while (w) {
    gt_task *const n = w->wnext;
    w->wnext = NULL;
    _gt_rq_push(w);
    w = n;
  }

  // The stack under our feet cannot be unmapped from here,
  // so hand it over and let whatever runs next do it.
  self->next = _gt_R.zombies;
  _gt_R.zombies = self;

  gt_task *const next = _gt_pick();
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
/// This, "_gt_await" and "_gt_detach" answer the public api
/// directly, so report sppc's error numbers (sppc/types.h).
_gnu_inline _gnu_restrict_access(write_only, 1) _gnu_nonnull(1)
int _gt_spawn(gt_handle_t *out, const sppc_closure body, void (*entry)(void)) {
  _gt_init();

  // Allocate a new task from the runtime, either from the
  // free slot list or by allocating new slots.
  gt_task *const t = _gt_slot_alloc();
  if (!t) { return SPPC_ENOMEM; }

  // Allocate a stack for this new task, based off the size
  // that the runtime has configured. Failing to acquire a
  // stack frees the slot too.
  const size_t size = _gt_R.stack_size;
  void *const stack = _gt_stack_alloc(size);
  if (!stack) {
    _gt_slot_free(t);
    return SPPC_ENOMEM;
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
      return SPPC_ENOMEM;
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
  if (_sppc_unlikely(!_gt_live())) { return; }
  gt_task *const self = _gt_R.current;
  if (_gt_R.inflight) { _gt_io_poll(0); }

  gt_task *const next = _gt_rq_pop();
  if (!next) { return; }
  _gt_rq_push(self);
  _gt_switch_to(next);
}

// ==================== THREAD PRIMITIVES ====================

/// Turn a relative timeout on "clock" into the absolute
/// deadline the pthread timed calls take.
_gnu_inline _gnu_nonnull(2, 3)
void _gt_deadline(const clockid_t clock, struct timespec const *timeout, struct timespec *out) {
  clock_gettime(clock, out);
  out->tv_sec += timeout->tv_sec;
  out->tv_nsec += timeout->tv_nsec;
  if (out->tv_nsec >= 1000000000L) {
    out->tv_sec += 1;
    out->tv_nsec -= 1000000000L;
  }
}

/// The time left until an absolute deadline, or zero if it
/// has passed.
_gnu_inline _gnu_nonnull(2)
struct timespec _gt_remaining(const clockid_t clock, struct timespec const *deadline) {
  struct timespec now;
  clock_gettime(clock, &now);
  struct timespec left = {deadline->tv_sec - now.tv_sec, deadline->tv_nsec - now.tv_nsec};
  if (left.tv_nsec < 0) {
    left.tv_sec -= 1;
    left.tv_nsec += 1000000000L;
  }
  if (left.tv_sec < 0) { left = (struct timespec){0, 0}; }
  return left;
}

/// Whether anything on this thread other than the current
/// task can make progress: a task ready to run, or one parked
/// on the ring.
_gnu_inline
int _gt_has_other_work(void) {
  if (!_gt_live()) { return 0; }
  if (_gt_R.inflight) { _gt_io_poll(0); }
  return _gt_R.rq_head != NULL || _gt_R.inflight != 0;
}

/// Let the rest of this thread move while the current task
/// cannot: run a ready task, or wait on the i/o backend for one to
/// become ready - no later than "deadline" if one is given.
/// Only call this once "_gt_has_other_work" has said yes.
_gnu_inline
void _gt_step(const clockid_t clock, struct timespec const *deadline) {
  gt_task *const next = _gt_rq_pop();
  if (next) {
    _gt_rq_push(_gt_R.current);
    _gt_switch_to(next);
    return;
  }
  if (deadline == NULL) {
    _gt_io_poll(1);
    return;
  }

  _gt_io_poll_until(clock, deadline);
}

/// Whether an absolute deadline has passed.
_gnu_inline _gnu_nonnull(2)
int _gt_passed(const clockid_t clock, struct timespec const *deadline) {
  const struct timespec left = _gt_remaining(clock, deadline);
  return left.tv_sec == 0 && left.tv_nsec == 0;
}

/// "_gt_passed" for a deadline that may be absent, which never
/// passes. Separate so a caller that has none can pass NULL
/// without the call to "_gt_passed" being spelt with it: some
/// gcc versions flag that call even where it cannot be reached.
_gnu_inline
int _gt_passed_if_any(const clockid_t clock, struct timespec const *deadline) {
  return deadline != NULL && _gt_passed(clock, deadline);
}

/// Take a task off the run queue, if it is on it.
_gnu_inline _gnu_nonnull(1)
int _gt_rq_remove(gt_task *t) {
  gt_task *prev = NULL;
  for (gt_task *cur = _gt_R.rq_head; cur != NULL; prev = cur, cur = cur->next) {
    if (cur != t) { continue; }
    if (prev) { prev->next = cur->next; }
    else { _gt_R.rq_head = cur->next; }
    if (_gt_R.rq_tail == cur) { _gt_R.rq_tail = prev; }
    cur->next = NULL;
    return 1;
  }
  return 0;
}

_gnu_inline _gnu_nonnull(1)
void _gt_lock_wait_add(gt_lock_waiter *w) {
  gt_lock_waiter **link = &_gt_R.lock_waiters;
  while (*link != NULL) { link = &(*link)->next; }
  w->next = NULL;
  *link = w;
}

_gnu_inline _gnu_nonnull(1)
void _gt_lock_wait_remove(gt_lock_waiter *w) {
  for (gt_lock_waiter **link = &_gt_R.lock_waiters; *link != NULL; link = &(*link)->next) {
    if (*link == w) {
      *link = w->next;
      return;
    }
  }
}

/// Called just after a thread lock is released. Without it the
/// releasing task takes the lock straight back on its next use -
/// a waiting task only sits on the run queue, and cannot try
/// again until something yields to it - so one task can hold a
/// shared lock for its whole run. Running the longest waiter
/// now, ahead of the releaser, lets it take the lock first. The
/// lock itself is not handed over: the waiter takes it as usual,
/// and a thread that got there first still wins.
_gnu_inline _gnu_nonnull(1)
void _gt_lock_released(void const *lock) {
  if (!_gt_live() || _gt_R.lock_waiters == NULL) { return; }
  for (gt_lock_waiter *w = _gt_R.lock_waiters; w != NULL; w = w->next) {
    if (w->lock != lock || w->task->state != GT_STATE_READY) { continue; }
    if (!_gt_rq_remove(w->task)) { continue; }
    _gt_rq_push(_gt_R.current);
    _gt_switch_to(w->task);
    return;
  }
}

/// Take a thread lock from a task. The holder may be a task on
/// this very thread, parked on the ring, and a blocking lock
/// would stop the only thread that can resume it. So a
/// contended lock lets the rest of the thread move - the holder
/// included, once its operation has completed - and only blocks
/// the thread when nothing else on it can, as the holder is
/// then on another. While it waits, the task is queued on the
/// lock so a release on this thread runs it next. The three
/// calls are the non-blocking try, the blocking lock and the
/// timed lock for one kind of lock; a deadline, if given, is
/// absolute. The result is stored into "err_out" - a statement
/// rather than an expression, as standard C has no way for a
/// macro to loop and still yield a value.
#define _gt_lock_until(err_out, lock, try_call, lock_call, timed_call, clock, deadline) do { \
  gt_lock_waiter waiter_ = {(lock), NULL, NULL};                                       \
  int queued_ = 0;                                                                     \
  for (;;) {                                                                           \
    (err_out) = (try_call);                                                            \
    if ((err_out) != EBUSY) { break; }                                                 \
    if (!_gt_has_other_work()) {                                                       \
      (err_out) = (deadline) ? (timed_call) : (lock_call);                             \
      break;                                                                           \
    }                                                                                  \
    if (_gt_passed_if_any((clock), (deadline))) {                                      \
      (err_out) = ETIMEDOUT;                                                           \
      break;                                                                           \
    }                                                                                  \
    if (!queued_) {                                                                    \
      waiter_.task = _gt_R.current;                                                    \
      _gt_lock_wait_add(&waiter_);                                                     \
      queued_ = 1;                                                                     \
    }                                                                                  \
    _gt_step((clock), (deadline));                                                     \
  }                                                                                    \
  if (queued_) { _gt_lock_wait_remove(&waiter_); }                                     \
} while (0)

_gnu_inline _gnu_nonnull(1)
int _gt_mutex_lock_until(pthread_mutex_t *mutex, const clockid_t clock, struct timespec const *deadline) {
  int err;
  _gt_lock_until(err, mutex,
    pthread_mutex_trylock(mutex), pthread_mutex_lock(mutex),
    _sppc_pthread_mutex_clocklock(mutex, clock, deadline), clock, deadline);
  return err;
}

_gnu_inline _gnu_nonnull(1)
int _gt_mutex_lock(pthread_mutex_t *mutex) {
  return _gt_mutex_lock_until(mutex, CLOCK_MONOTONIC, NULL);
}

_gnu_inline _gnu_nonnull(1)
int _gt_mutex_unlock(pthread_mutex_t *mutex) {
  const int err = pthread_mutex_unlock(mutex);
  if (err == 0) { _gt_lock_released(mutex); }
  return err;
}

_gnu_inline _gnu_nonnull(1)
int _gt_rwlock_rdlock_until(pthread_rwlock_t *rwlock, const clockid_t clock, struct timespec const *deadline) {
  int err;
  _gt_lock_until(err, rwlock,
    _sppc_pthread_rwlock_tryrdlock(rwlock), pthread_rwlock_rdlock(rwlock),
    _sppc_pthread_rwlock_clockrdlock(rwlock, clock, deadline), clock, deadline);
  return err;
}

_gnu_inline _gnu_nonnull(1)
int _gt_rwlock_wrlock_until(pthread_rwlock_t *rwlock, const clockid_t clock, struct timespec const *deadline) {
  int err;
  _gt_lock_until(err, rwlock,
    _sppc_pthread_rwlock_trywrlock(rwlock), pthread_rwlock_wrlock(rwlock),
    _sppc_pthread_rwlock_clockwrlock(rwlock, clock, deadline), clock, deadline);
  return err;
}

_gnu_inline _gnu_nonnull(1)
int _gt_rwlock_unlock(pthread_rwlock_t *rwlock) {
  const int err = pthread_rwlock_unlock(rwlock);
  if (err == 0) { _gt_lock_released(rwlock); }
  return err;
}

/// A spin lock has no timed form, and no deadline is ever
/// passed here, so the timed call is never made.
_gnu_inline _gnu_nonnull(1)
int _gt_spin_lock(_sppc_spinlock_t *spinlock) {
  int err;
  _gt_lock_until(err, (void const*)spinlock,
    _sppc_pthread_spin_trylock(spinlock), _sppc_pthread_spin_lock(spinlock),
    _sppc_pthread_spin_lock(spinlock), CLOCK_MONOTONIC, (struct timespec const*)NULL);
  return err;
}

_gnu_inline _gnu_nonnull(1)
int _gt_spin_unlock(_sppc_spinlock_t *spinlock) {
  const int err = _sppc_pthread_spin_unlock(spinlock);
  if (err == 0) { _gt_lock_released((void const*)spinlock); }
  return err;
}

/// Wait on a condition from a task. If only another thread
/// can signal, the real wait is right. If a task on this thread
/// might, a real wait would stop it for good - so the mutex is
/// dropped, the thread moves on, and this returns as a spurious
/// wake-up. Condition waits are allowed those, and every caller
/// re-checks its predicate under the lock anyway. A deadline,
/// if given, is absolute.
_gnu_inline _gnu_nonnull(1, 2)
int _gt_cond_wait_until(pthread_cond_t *cond, pthread_mutex_t *mutex, const clockid_t clock,
  struct timespec const *deadline) {
  if (!_gt_has_other_work()) {
    return deadline ? _sppc_pthread_cond_clockwait(cond, mutex, clock, deadline) : pthread_cond_wait(cond, mutex);
  }

  // Releasing may already run a task waiting on the mutex; the
  // step after it still lets the rest of the thread move, the
  // signaller included, before the lock is taken back.
  const int uerr = _gt_mutex_unlock(mutex);
  if (uerr != 0) { return uerr; }
  if (_gt_has_other_work()) { _gt_step(clock, deadline); }

  // The mutex is held again on the way out whatever happened,
  // as a real wait would leave it, so no deadline applies here.
  const int lerr = _gt_mutex_lock(mutex);
  if (lerr != 0) { return lerr; }
  return deadline && _gt_passed(clock, deadline) ? ETIMEDOUT : 0;
}

/// Wait for a task to finish. The value it produced is not
/// read here as it is already in the cell the caller gave the
/// closure; so this only has to make the wait happen and
/// retire the slot afterwards.
_gnu_inline
int _gt_await(const gt_handle_t handle) {
  if (_sppc_unlikely(!_gt_live())) { return SPPC_EINVAL; }

  gt_task *const t = _gt_resolve(handle);

  // A handle whose slot has been retired no longer resolves,
  // so awaiting the same task twice is refused rather than
  // answered with whatever now occupies the slot. S++ memory
  // rules prevent this from happening anyway (consuming self).
  if (t == NULL) { return SPPC_EINVAL; }
  if (t == _gt_R.current) { return SPPC_EDEADLK; }
  if (t->detached) { return SPPC_EINVAL; }

  if (t->state != GT_STATE_DONE) {
    gt_task *const self = _gt_R.current;
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
  if (_sppc_unlikely(!_gt_live())) { return SPPC_EINVAL; }

  gt_task *const t = _gt_resolve(handle);
  if (t == NULL) { return SPPC_EINVAL; }
  if (t == _gt_R.current) { return SPPC_EINVAL; }
  if (t->detached) { return SPPC_EINVAL; }

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
  if (!_gt_live()) { return; }

  gt_task *const self = _gt_R.current;
  for (;;) {
    if (_gt_R.rq_head == NULL) {
      if (_gt_R.inflight == 0) { break; }
      _gt_io_poll(1);
      continue;
    }
    gt_task *const next = _gt_rq_pop();
    _gt_rq_push(self);
    _gt_switch_to(next);
  }

  if (_gt_R.zombies != NULL) { _gt_reap(); }
  _gt_io_teardown();
}

/// Release everything the runtime holds on this thread: the ring,
/// every task's stacks and slot, the slot table, the stack cache and
/// the runtime block itself. Tasks still outstanding are abandoned -
/// nothing on a thread that is going away can run them - and the
/// environment a never-finished task captured is freed with it, as
/// a spawn that fails frees it. A thread made by "sppc_pthread_create"
/// drains first, so it only gets here with nothing left; a thread
/// made elsewhere may not have.
_gnu_noinline _gnu_cold
inline void _gt_teardown(void) {
  _gt_io_teardown();
  if (_gt_R.zombies != NULL) { _gt_reap(); }

  // The stack this runs on cannot be unmapped from under it. That is
  // only a task's own stack if the thread exited from inside a task.
  void *const live_stack = _gt_R.current ? _gt_R.current->stack : NULL;

  for (uint32_t i = 0; i < _gt_R.nslots; ++i) {
    gt_task *const t = _gt_R.slots[i];
    if (t->stack && t->stack != live_stack) { _gt_stack_unmap(t->stack, t->stack_size); }
    if (t->ustack) { _gt_stack_unmap(t->ustack, t->ustack_size); }
    if (t->state != GT_STATE_FREE && t->state != GT_STATE_DONE) { free(t->body.env); }
    free(t);
  }
  free(_gt_R.slots);

  while (_gt_R.stack_cached) {
    _gt_stack_unmap(_gt_R.stack_cache[--_gt_R.stack_cached], _gt_R.cached_size);
  }

  free(_gt_rt);
  _gt_rt = NULL;
}

/// The key's destructor, run at thread exit for a thread that
/// started the runtime.
_gnu_inline_va
void _gt_rt_free(void *rt) {
  if (rt == _gt_rt) { _gt_teardown(); }
}
