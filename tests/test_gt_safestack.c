#include <sppc/sppc.h>
#include <assert.h>

// Each task must run on an unsafe stack of its own, and the pointer must be
// restored when it is switched back to. Without that, tasks interleave on one
// unsafe stack, and because switching is not LIFO a task that resumes and
// returns unwinds it past frames another task is still using.
static void *seen[4]; static int seen_n = 0;
typedef struct { int id; void **slot; } env_t;

static void body(void *e) {
  env_t v = *(env_t*)e;
  void *first = __safestack_unsafe_stack_ptr;
  *v.slot = first;
  seen[seen_n++] = first;
  sppc_async_yield();                        // let the others run and move the pointer
  void *after = __safestack_unsafe_stack_ptr;
  assert(after == first && "the unsafe stack pointer must come back unchanged");
  sppc_async_yield();
  assert(__safestack_unsafe_stack_ptr == first);
}

int main(void) {
  assert(sppc_init() == 0);
  assert(sppc_unsafe_stack_up() == 0);       // marks split stacks as wanted
  void *main_usp = __safestack_unsafe_stack_ptr;
  assert(main_usp != NULL);

  void *slots[3] = {0};
  int h[3];
  for (int i = 0; i < 3; ++i) {
    env_t *e = malloc(sizeof *e); e->id = i; e->slot = &slots[i];
    assert(sppc_async(&h[i], (sppc_closure){body, e}) == 0);
  }
  for (int i = 0; i < 3; ++i) assert(sppc_await(h[i]) == 0);

  printf("main usp   = %p\n", main_usp);
  for (int i = 0; i < 3; ++i) printf("task %d usp = %p\n", i, slots[i]);

  // Every task, and the thread itself, must be on a different unsafe stack.
  assert(slots[0] && slots[1] && slots[2]);
  assert(slots[0] != slots[1] && slots[1] != slots[2] && slots[0] != slots[2]);
  assert(slots[0] != main_usp && slots[1] != main_usp && slots[2] != main_usp);

  // And the thread's own pointer must survive the whole thing.
  assert(__safestack_unsafe_stack_ptr == main_usp);
  printf("ALL OK\n");
  return 0;
}
