#include <sppc/sppc.h>
#include <assert.h>

// Mirrors what the s++ side will do: the environment is heap-owned and the
// runtime frees it, and anything the caller wants back lives in a cell the
// caller allocated and the closure captured a pointer to.
static int order[64];
static int order_n = 0;
static long sum_cell = 0;

typedef struct { int id; int spins; } env_t;
static void body(void *e) {
  env_t v = *(env_t*)e;
  for (int i = 0; i < v.spins; ++i) { order[order_n++] = v.id; sppc_async_yield(); }
  sum_cell += v.id;
}
static sppc_closure mk(int id, int spins) {
  env_t *e = malloc(sizeof *e); e->id = id; e->spins = spins;
  return (sppc_closure){body, e};
}

typedef struct { long *out; } deep_env_t;
static long deep(int n) { volatile char pad[512]; pad[0] = (char)n; return n <= 0 ? 0 : pad[0] + deep(n - 1); }
static void deep_body(void *e) { *((deep_env_t*)e)->out = deep(100); }

// Awaits a task from inside another task, to exercise the waiter list.
typedef struct { int target; int *flag; } join_env_t;
static void join_body(void *e) {
  join_env_t v = *(join_env_t*)e;
  assert(sppc_await(v.target) == 0);
  *v.flag += 1;
}

int main(void) {
  assert(sppc_init() == 0);

  int ha, hb, hc;
  assert(sppc_async(&ha, mk(1, 2)) == 0);
  assert(sppc_async(&hb, mk(2, 2)) == 0);
  assert(sppc_async(&hc, mk(3, 2)) == 0);
  assert(sppc_await(ha) == 0);
  assert(sppc_await(hb) == 0);
  assert(sppc_await(hc) == 0);
  printf("order:"); for (int i = 0; i < order_n; ++i) printf(" %d", order[i]); printf("\n");
  printf("sum=%ld (want 6)\n", sum_cell);
  assert(sum_cell == 6);
  assert(sppc_await(ha) == SPPC_EINVAL);   // retired handle is refused, not answered

  long deep_out = 0;
  deep_env_t *de = malloc(sizeof *de); de->out = &deep_out;
  int hd; assert(sppc_async(&hd, (sppc_closure){deep_body, de}) == 0);
  assert(sppc_await(hd) == 0);
  printf("deep=%ld (want 5050)\n", deep_out);
  assert(deep_out == 5050);

  // One task, three awaiters: the old single-waiter slot lost all but the last.
  int slow; assert(sppc_async(&slow, mk(9, 4)) == 0);
  int flag = 0, j1, j2, j3;
  for (int i = 0; i < 3; ++i) {
    join_env_t *je = malloc(sizeof *je); je->target = slow; je->flag = &flag;
    int *h = i == 0 ? &j1 : i == 1 ? &j2 : &j3;
    assert(sppc_async(h, (sppc_closure){join_body, je}) == 0);
  }
  // Only the first awaiter retires the slot; the others then see EINVAL. What
  // matters is that all three are woken rather than left parked forever.
  assert(sppc_await(j1) == 0);
  assert(sppc_await(j2) == 0);
  assert(sppc_await(j3) == 0);
  printf("waiters woken=%d (want 3)\n", flag);
  assert(flag == 3);
  // The slot must have been retired exactly once. If every awaiter retired it,
  // the free list is a cycle and the next few spawns hand back the same slot.
  int s1, s2, s3;
  assert(sppc_async(&s1, mk(0, 0)) == 0);
  assert(sppc_async(&s2, mk(0, 0)) == 0);
  assert(sppc_async(&s3, mk(0, 0)) == 0);
  assert(s1 != s2 && s2 != s3 && s1 != s3);
  assert(sppc_await(s1) == 0 && sppc_await(s2) == 0 && sppc_await(s3) == 0);
  printf("distinct slots after multi-await: %d %d %d\n", s1, s2, s3);

  int he; assert(sppc_async(&he, mk(4, 1)) == 0);
  assert(sppc_async_detach(he) == 0);
  assert(sppc_cleanup() == 0);
  printf("sum after detach=%ld (want 19)\n", sum_cell);
  assert(sum_cell == 19);

  printf("ALL OK\n");
  return 0;
}
