#pragma once
#include <unistd.h>
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

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

static gt_task task_pool[MAX_TASKS];
static auto task_free = 0;
static gt_task *ready_head = NULL;
static gt_task *ready_tail = NULL;
static gt_task *current = NULL;
static gt_ctx main_ctx;

extern void gt_switch(gt_ctx *old, gt_ctx *new);

_gnu_inline _gnu_hot _gnu_nonnull(1)
static void enqueue(gt_task *t) {
  // Set the next task of the new one to nullptr. Then, update
  // the current tail's next task to this one, or set the head
  // task to this one should the queue be empty. Mark this task
  // as the new tail.
  t->next = NULL;
  if (ready_tail) ready_tail->next = t;
  else ready_head = t;
  ready_tail = t;
}

_gnu_inline _gnu_hot
static gt_task* dequeue(void) {
  // Get the task from the head of the task queue. Update the
  // head of the queue to the task after the dequeued one, and
  // set the next task of the dequeued one to nullptr (fully
  // detached from the queue).
  const auto t = ready_head;
  if (!t) return NULL;
  ready_head = t->next;
  if (!ready_head) ready_tail = NULL;
  t->next = NULL;
  return t;
}

_gnu_inline _gnu_hot
static void* alloc_stack(void) {
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
static void free_stack(void *p) {
  // De-allocate the stack, taking the stack size and the
  // guard sizes into account when calculating pointer position
  // and size.
  auto total = STACK_SIZE + GUARD_SIZE;
  const auto page = (size_t)sysconf(_SC_PAGESIZE);
  total = (total + page - 1) & ~(page - 1);
  munmap(p, total);
}

_gnu_inline _gnu_hot
static void task_entry(void) {
  const auto self = current;
  const auto res = self->fn(self->argc, self->argv);
  self->result = res;
  self->done = 1;

  if (self->waiter) {
    enqueue(self->waiter);
    self->waiter = NULL;
  }

  const auto next = dequeue();
  if (!next) {
    gt_switch(&self->ctx, &main_ctx);
    abort();
  }
  current = next;
  gt_switch(&self->ctx, &next->ctx);
}

// CORE

_gnu_inline _gnu_cold
static void gt_init(void) {
  current = NULL;
  ready_head = ready_tail = NULL;
  task_free = 0;
}

_gnu_inline _gnu_hot _gnu_nonnull(1)
static gt_task* gt_spawn(gt_entry_fn fn) {
  if (task_free >= MAX_TASKS) { abort(); }
  const auto t = &task_pool[task_free];
  memset(t, 0, sizeof(*t));
  t->fn = fn;
  t->stack = alloc_stack();
  if (!t->stack) { return NULL; }
  task_free++;

  auto sp = (uint64_t*)((char*)t->stack + STACK_SIZE);
  sp = (uint64_t*)((uintptr_t)sp & ~0xFULL);
  *--sp = (uint64_t)task_entry;

  t->ctx.rsp = (uint64_t)sp;
  enqueue(t);
  return t;
}

_gnu_inline _gnu_hot
static void gt_yield(void) {
  const auto next = dequeue();
  if (!next) return;
  if (current) enqueue(current);
  const auto prev = current;
  current = next;
  if (prev) gt_switch(&prev->ctx, &next->ctx);
  else gt_switch(&main_ctx, &next->ctx);
}

_gnu_inline _gnu_hot _gnu_nonnull(1)
static void* gt_await(gt_task *task) {
  if (task->dead) { return task->result; }
  if (!task->done) {
    if (current) task->waiter = current;
    else {
      while (!task->done) gt_yield();
      goto done;
    }
    const auto next = dequeue();
    if (!next) {
      perror("[sppc] [CRITICAL] gt deadlock");
      abort();
    }

    const auto prev = current;
    current = next;
    gt_switch(&prev->ctx, &next->ctx);
  }
done:
  const auto res = task->result;
  free_stack(task->stack);
  task->dead = 1;
  return res;
}

