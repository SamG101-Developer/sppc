#include <sppc/sppc.h>
#include <assert.h>

// Mirrors what the std tests do to the runtime: many rounds of spawn/await,
// some detached, some yielding, with captured environments of differing sizes.
typedef struct { long *out; long v; char pad[24]; } env_t;
static void body(void *e) { env_t *v = (env_t*)e; *v->out += v->v; }
static void yielding(void *e) {
  env_t *v = (env_t*)e;
  sppc_async_yield();
  sppc_async_yield();
  *v->out += v->v;
}
static sppc_closure mk(void (*fn)(void*), long *out, long v) {
  env_t *e = malloc(sizeof *e);
  e->out = out; e->v = v;
  return (sppc_closure){fn, e};
}

int main(void) {
  assert(sppc_init() == 0);
  assert(sppc_unsafe_stack_up() == 0);
  long total = 0;

  for (int round = 0; round < 40; ++round) {
    // A batch awaited in order.
    int h[16];
    for (int i = 0; i < 16; ++i) assert(sppc_async(&h[i], mk(body, &total, 1)) == 0);
    for (int i = 0; i < 16; ++i) assert(sppc_await(h[i]) == 0);

    // A batch awaited in reverse, so slots come back out of order.
    for (int i = 0; i < 16; ++i) assert(sppc_async(&h[i], mk(body, &total, 1)) == 0);
    for (int i = 15; i >= 0; --i) assert(sppc_await(h[i]) == 0);

    // Yielding tasks interleaved with plain ones.
    int a, b, c;
    assert(sppc_async(&a, mk(yielding, &total, 1)) == 0);
    assert(sppc_async(&b, mk(body, &total, 1)) == 0);
    assert(sppc_async(&c, mk(yielding, &total, 1)) == 0);
    assert(sppc_await(a) == 0 && sppc_await(b) == 0 && sppc_await(c) == 0);

    // Detached, never awaited.
    int d;
    assert(sppc_async(&d, mk(body, &total, 1)) == 0);
    assert(sppc_async_detach(d) == 0);

    // Some heap traffic between rounds, so corruption shows up as malloc noise.
    for (int i = 0; i < 64; ++i) { void *p = malloc(48 + i); free(p); }
  }

  assert(sppc_cleanup() == 0);
  printf("total=%ld (want %d)\n", total, 40 * (16 + 16 + 3 + 1));
  assert(total == 40 * (16 + 16 + 3 + 1));
  printf("ALL OK\n");
  return 0;
}
