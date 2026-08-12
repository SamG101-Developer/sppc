#pragma once
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <pthread.h>
#include <sppc/macros.h>

#define STACK_SIZE (64 * 1024)
#define GUARD_SIZE 4096
#define MAX_TASKS 65536
#define GT_MAX_ARGS 16

typedef struct gt_task gt_task;

typedef struct {
  uint64_t rsp, rbp, rbx, r12, r13, r14, r15;
} gt_ctx;

typedef void*(*gt_entry_fn)(size_t argc, uintptr_t const *argv);

struct gt_task {
  gt_ctx ctx;
  void *stack;
  void *result;
  int done;
  int dead;
  gt_task *waiter;
  gt_task *next;
  gt_entry_fn fn;
  size_t argc;
  uintptr_t argv[GT_MAX_ARGS];
  uint32_t generation;
};

extern _Thread_local gt_task *gt_task_pool;
extern _Thread_local int gt_task_free;
extern _Thread_local gt_task *gt_ready_head;
extern _Thread_local gt_task *gt_ready_tail;
extern _Thread_local gt_task *gt_current;
extern _Thread_local gt_task *gt_free_head;
extern _Thread_local gt_ctx gt_main_ctx;

#define GT_POOL_BYTES ((size_t)MAX_TASKS * sizeof(gt_task))

// Reclaims a thread's pool when it exits, via a pthread key.
extern pthread_key_t gt_pool_key;
extern pthread_once_t gt_pool_key_once;

_gnu_inline _gnu_cold
void gt_pool_release(void *pool) {
  if (pool) { munmap(pool, GT_POOL_BYTES); }
}

_gnu_inline _gnu_cold
void gt_pool_key_init(void) {
  pthread_key_create(&gt_pool_key, gt_pool_release);
}

// Maps this thread's pool on first use. NULL means the mapping failed.
_gnu_inline
gt_task* gt_pool(void) {
  if (gt_task_pool != NULL) { return gt_task_pool; }
  pthread_once(&gt_pool_key_once, gt_pool_key_init);

  const auto p = mmap(NULL, GT_POOL_BYTES, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (p == MAP_FAILED) { return NULL; }

  gt_task_pool = p;
  pthread_setspecific(gt_pool_key, p);
  return gt_task_pool;
}

extern void gt_switch(gt_ctx *old, gt_ctx *new);

_gnu_inline _gnu_hot _gnu_nonnull(1)
void gt_enqueue(gt_task *t) {
  // Set the next task of the new one to nullptr. Then, update
  // the current tail's next task to this one, or set the head
  // task to this one should the queue be empty. Mark this task
  // as the new tail.
  t->next = NULL;
  if (gt_ready_tail) gt_ready_tail->next = t;
  else gt_ready_head = t;
  gt_ready_tail = t;
}

_gnu_inline _gnu_hot
gt_task* gt_dequeue(void) {
  // Get the task from the head of the task queue. Update the
  // head of the queue to the task after the dequeued one, and
  // set the next task of the dequeued one to nullptr (fully
  // detached from the queue).
  const auto t = gt_ready_head;
  if (!t) return NULL;
  gt_ready_head = t->next;
  if (!gt_ready_head) gt_ready_tail = NULL;
  t->next = NULL;
  return t;
}

_gnu_inline _gnu_hot
void* gt_alloc_stack(void) {
  // Allocate a stack for the context of the async call using
  // stack size and guard size, and then align it against the
  // pagesize.
  auto total = STACK_SIZE + GUARD_SIZE;
  const auto page = (size_t)sysconf(_SC_PAGESIZE);
  total = (total + page - 1) & ~(page - 1);

  // Perform the allocation using the `mmap` function, and
  // handle a failed allocation.
  const auto p = mmap(
    NULL, total, PROT_READ | PROT_WRITE,
    MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
  if (p == MAP_FAILED) { return nullptr; }

  // Protect the guard region, unmapping on failure, and then
  // return the pointer to the start of the allocated block.
  if (mprotect(p, GUARD_SIZE, PROT_NONE) != 0) {
    munmap(p, total);
    return nullptr;
  }
  return p;
}

_gnu_inline _gnu_nonnull(1)
void gt_free_stack(void *p) {
  // De-allocate the stack, taking the stack size and the
  // guard sizes into account when calculating pointer position
  // and size.
  auto total = STACK_SIZE + GUARD_SIZE;
  const auto page = (size_t)sysconf(_SC_PAGESIZE);
  total = (total + page - 1) & ~(page - 1);
  munmap(p, total);
}

// Address is taken in gt_spawn, so this one needs a real out-of-line copy.
_gnu_inline_va _gnu_hot
void gt_task_entry(void) {
  const auto self = gt_current;
  const auto res = self->fn(self->argc, self->argv);
  self->result = res;
  self->done = 1;

  if (self->waiter) {
    gt_enqueue(self->waiter);
    self->waiter = NULL;
  }

  const auto next = gt_dequeue();
  if (!next) {
    // Control is going back to the main context, which is not a task, so
    // gt_current has to say so. Leaving it pointing at this finished task
    // made the next await from main treat itself as running inside one.
    gt_current = NULL;
    gt_switch(&self->ctx, &gt_main_ctx);
    abort();
  }
  gt_current = next;
  gt_switch(&self->ctx, &next->ctx);
}

// CORE

_gnu_inline _gnu_cold
void gt_init(void) {
  gt_current = NULL;
  gt_ready_head = gt_ready_tail = NULL;
  gt_free_head = NULL;
  gt_task_free = 0;
}

_gnu_inline _gnu_hot _gnu_nonnull(1)
size_t gt_handle(gt_task const *t) {
  // A handle is the slot index paired with that slot's generation. Slots are
  // recycled, so a bare pointer would let a stale handle address a task that
  // has since been reused; the generation makes that detectable. Generations
  // start at 1, so a valid handle is never 0.
  return ((size_t)(t - gt_task_pool) << 32) | t->generation;
}

_gnu_inline _gnu_hot
gt_task* gt_resolve(const size_t handle) {
  const auto index = handle >> 32;
  const auto generation = (uint32_t)handle;
  if (generation == 0 || index >= MAX_TASKS || gt_task_pool == NULL) { return NULL; }
  const auto t = &gt_task_pool[index];
  return t->generation == generation ? t : NULL;
}

_gnu_inline _gnu_hot _gnu_nonnull(1)
gt_task* gt_spawn(gt_entry_fn fn) {
  // Prefer a slot released by a completed task; only grow the pool when
  // none are free. Without this the pool was consumed permanently and the
  // 65537th spawn of a process aborted, however many had finished.
  const auto pool = gt_pool();
  if (pool == NULL) { return NULL; }

  auto t = gt_free_head;
  if (t == NULL && gt_task_free >= MAX_TASKS) { abort(); }
  if (t == NULL) { t = &pool[gt_task_free]; }

  // Allocate before committing the slot, so a failure consumes nothing.
  const auto stack = gt_alloc_stack();
  if (!stack) { return NULL; }
  if (t == gt_free_head) { gt_free_head = t->next; }
  else { gt_task_free++; }

  const auto generation = t->generation;
  memset(t, 0, sizeof(*t));
  t->generation = generation ? generation : 1;
  t->fn = fn;
  t->stack = stack;

  auto sp = (uint64_t*)((char*)t->stack + STACK_SIZE);
  sp = (uint64_t*)((uintptr_t)sp & ~0xFULL);

  // The entry address must sit on a 16-byte boundary, not just below one:
  // gt_switch reaches the task via `ret`, which pops 8 bytes, and the SysV
  // ABI wants rsp % 16 == 8 at a function's first instruction. Landing on
  // 0 instead breaks every aligned SSE access in the task and its callees.
  sp -= 2;
  *sp = (uint64_t)gt_task_entry;

  t->ctx.rsp = (uint64_t)sp;
  gt_enqueue(t);
  return t;
}

_gnu_inline _gnu_hot
void gt_yield(void) {
  const auto next = gt_dequeue();
  if (!next) return;
  if (gt_current) gt_enqueue(gt_current);
  const auto prev = gt_current;
  gt_current = next;
  if (prev) gt_switch(&prev->ctx, &next->ctx);
  else gt_switch(&gt_main_ctx, &next->ctx);
}

_gnu_inline _gnu_hot _gnu_nonnull(1)
void* gt_await(gt_task *task) {
  if (task->dead) { return task->result; }
  if (!task->done) {
    if (gt_current) task->waiter = gt_current;
    else {
      // Nothing runnable and the task is not done means nothing ever will
      // make it done. This used to spin at 100% CPU forever.
      while (!task->done) {
        if (gt_ready_head == NULL) {
          perror("[sppc] [CRITICAL] gt deadlock");
          abort();
        }
        gt_yield();
      }
      goto done;
    }
    const auto next = gt_dequeue();
    if (!next) {
      perror("[sppc] [CRITICAL] gt deadlock");
      abort();
    }

    const auto prev = gt_current;
    gt_current = next;
    gt_switch(&prev->ctx, &next->ctx);
  }
done:
  const auto res = task->result;
  gt_free_stack(task->stack);
  task->stack = NULL;
  task->dead = 1;

  // Retire the slot. Bumping the generation invalidates any handle still
  // held for it, so a stale await fails to resolve rather than reading
  // whatever task now occupies the slot.
  task->generation++;
  task->next = gt_free_head;
  gt_free_head = task;
  return res;
}
