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
#include <sppc/macros.h>

#define STACK_SIZE (64 * 1024)
#define GUARD_SIZE 4096
#define MAX_TASKS 65536
#define GT_MAX_ARGS 16

typedef struct gt_task gt_task;

typedef struct {
  uint64_t rsp, rbp, rbx, r12, r13, r14, r15;
} gt_ctx;

typedef void *(*gt_entry_fn)(size_t argc, uintptr_t const *argv);

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
};

// Runtime state is defined once in sources/sppc/sppc.c. It
// must not be static here as every TU including this header
// would otherwise get a private scheduler (and a private
// task_pool), so tasks spawned in one TU would be invisible
// to another.
extern gt_task gt_task_pool[MAX_TASKS];
extern int gt_task_free;
extern gt_task *gt_ready_head;
extern gt_task *gt_ready_tail;
extern gt_task *gt_current;
extern gt_ctx gt_main_ctx;

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
  gt_task_free = 0;
}

_gnu_inline _gnu_hot _gnu_nonnull(1)
gt_task* gt_spawn(gt_entry_fn fn) {
  if (gt_task_free >= MAX_TASKS) { abort(); }
  const auto t = &gt_task_pool[gt_task_free];
  memset(t, 0, sizeof(*t));
  t->fn = fn;
  t->stack = gt_alloc_stack();
  if (!t->stack) { return NULL; }
  gt_task_free++;

  auto sp = (uint64_t*)((char*)t->stack + STACK_SIZE);
  sp = (uint64_t*)((uintptr_t)sp & ~0xFULL);
  *--sp = (uint64_t)gt_task_entry;

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
      while (!task->done) gt_yield();
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
  task->dead = 1;
  return res;
}
