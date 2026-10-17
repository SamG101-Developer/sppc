#include <sppc/sppc.h>
#include <assert.h>

static char log_[128]; static int log_n = 0;
static void note(char c) { log_[log_n++] = c; }

typedef struct { int fd; char *buf; ssize_t *out; } rd_t;
static void reader(void *e) {
  rd_t v = *(rd_t*)e;
  note('R');                       // reader starts
  ssize_t r;
  int took = _gt_try_read(v.fd, v.buf, 16, &r);
  assert(took);                    // the ring must have taken it
  *v.out = r;
  note('r');                       // reader finishes
}

typedef struct { int fd; } wr_t;
static void writer(void *e) {
  wr_t v = *(wr_t*)e;
  note('W');                       // writer runs *while* the reader is parked
  ssize_t r;
  if (!_gt_try_write(v.fd, "hello", 5, &r)) { r = write(v.fd, "hello", 5); }
  assert(r == 5);
  note('w');
}

static void sleeper(void *e) {
  (void)e;
  int r;
  const struct timespec d = {0, 40 * 1000 * 1000};   // 40ms
  int took = _gt_try_sleep(CLOCK_MONOTONIC, 0, &d, &r);
  assert(took && r == 0);
  note('s');
}
// The same, through the call std's "sleep" makes: relative, on std's default clock.
static void public_sleeper(void *e) {
  (void)e;
  const sppc_timespec d = {0, 40 * 1000 * 1000};
  assert(sppc_clock_nanosleep(SPPC_CLOCK_REALTIME, 0, &d) == 0);
  note('s');
}
// And as an absolute deadline, the way "sleep_until" asks.
static void deadline_sleeper(void *e) {
  (void)e;
  sppc_timespec t;
  assert(sppc_clock_gettime(SPPC_CLOCK_MONOTONIC, &t) == 0);
  t.nsec += 40 * 1000 * 1000;
  if (t.nsec >= 1000 * 1000 * 1000) { t.sec += 1; t.nsec -= 1000 * 1000 * 1000; }
  assert(sppc_clock_nanosleep(SPPC_CLOCK_MONOTONIC, SPPC_TIMER_ABSTIME, &t) == 0);
  note('s');
}
static void spinner(void *e) {
  (void)e;
  for (int i = 0; i < 3; ++i) { note('.'); sppc_async_yield(); }
}

// A sleeping task must not stop the others: the spinner runs to completion first.
static void check_sleep_does_not_stall(void (*sleep_fn)(void*), char const *what) {
  log_n = 0;
  int hs, hp;
  assert(sppc_async(&hs, (sppc_closure){sleep_fn, NULL}) == 0);
  assert(sppc_async(&hp, (sppc_closure){spinner, NULL}) == 0);
  assert(sppc_await(hs) == 0);
  assert(sppc_await(hp) == 0);
  log_[log_n] = 0;
  printf("%s order=%s (want ...s)\n", what, log_);
  assert(strcmp(log_, "...s") == 0);
}

int main(void) {
  assert(sppc_init() == 0);
  printf("io_uring available: %s\n", _gt_io_ready() ? "yes" : "no");
  if (!_gt_io_ready()) { printf("SKIPPED (no ring)\n"); return 77; }

  int fds[2]; assert(pipe(fds) == 0);
  char buf[16] = {0}; ssize_t got = -1;

  rd_t *re = malloc(sizeof *re); *re = (rd_t){fds[0], buf, &got};
  wr_t *we = malloc(sizeof *we); *we = (wr_t){fds[1]};
  int hr, hw;
  assert(sppc_async(&hr, (sppc_closure){reader, re}) == 0);
  assert(sppc_async(&hw, (sppc_closure){writer, we}) == 0);
  assert(sppc_await(hr) == 0);
  assert(sppc_await(hw) == 0);

  log_[log_n] = 0;
  printf("order=%s (want RWwr: the reader parks, the writer runs, then the reader wakes)\n", log_);
  printf("read %zd bytes: %.5s\n", got, buf);
  assert(got == 5 && memcmp(buf, "hello", 5) == 0);
  assert(strcmp(log_, "RWwr") == 0);

  check_sleep_does_not_stall(sleeper, "sleep");
  check_sleep_does_not_stall(public_sleeper, "sppc_clock_nanosleep");
  check_sleep_does_not_stall(deadline_sleeper, "sppc_clock_nanosleep (deadline)");

  // A clock the ring cannot time against falls back to the plain call, and its error is reported rather than lost.
  const struct timespec d = {0, 1000};
  const sppc_timespec sd = {0, 1000};
  int r;
  assert(_gt_try_sleep(CLOCK_THREAD_CPUTIME_ID, 0, &d, &r) == 0);
  assert(sppc_clock_nanosleep(SPPC_CLOCK_THREAD_CPUTIME_ID, 0, &sd) == SPPC_EINVAL);
  printf("unsupported clock falls back and reports EINVAL\n");

  assert(sppc_cleanup() == 0);
  printf("ALL OK\n");
  return 0;
}
