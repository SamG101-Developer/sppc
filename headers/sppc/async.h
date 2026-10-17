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

#include <sppc/config.h>

// As in sppc.h: "inline" with "noinline" is deliberate on the
// runtime's cold paths (one emitted copy, kept out of its
// callers), which gcc warns about.
#if SPPC_CC_GCC || SPPC_CC_CLANG
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes"
#endif

#include <sppc/async/runtime.h>
#include <sppc/async/vmem.h>
#include <sppc/async/context.h>
#include <sppc/async/sched.h>
#if SPPC_ASYNC_IO_URING
  #include <sppc/async/io_uring.h>
#else
  #include <sppc/async/blocking.h>
#endif

// ==================== PUBLIC API ====================

/// The body is a closure rather than a function and an argument
/// list, which is what lets an s++ callable cross intact: its
/// captures ride along in the environment, and its result goes
/// to a cell the caller allocated and the closure captured.
_gnu_inline _gnu_restrict_access(write_only, 1) _gnu_nonnull(1)
_sppc_api int sppc_async(int *handle, const sppc_closure body) {
  const int err = _gt_spawn(handle, body, _gt_trampoline);
  if (err != 0) { free(body.env); }
  return err;
}

/// Nothing is returned but the error: the value the task produced
/// is already in the caller's own cell by the time this returns.
_gnu_inline
_sppc_api int sppc_await(const int handle) {
  return _gt_await(handle);
}

/// Detach an asynchronous task by the handle.
_gnu_inline
_sppc_api int sppc_async_detach(const int handle) {
  return _gt_detach(handle);
}

/// Yield an asynchronous task.
_gnu_inline
_sppc_api void sppc_async_yield(void) {
  _gt_yield();
}

/// Simple setter for the stack size for asynchronous tasks' private
/// stacks.
_gnu_inline
_sppc_api void sppc_async_stack_size(const size_t bytes) {
  _gt_init();
  _gt_set_stack_size(bytes);
}

#if SPPC_CC_GCC || SPPC_CC_CLANG
#pragma GCC diagnostic pop
#endif
