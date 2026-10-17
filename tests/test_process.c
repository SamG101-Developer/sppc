#include "check.h"

// The process: its environment, ids, randomness and standard streams, and
// every way it ends - exit, halt, abort, a failed runtime check, a fatal
// signal - each run in a child, checked by its status and what it printed.

// ==================== ENVIRONMENT ====================

static void check_environment(void) {
  char buf[64];
  CHECK(sppc_unsetenv("SPPC_TEST_VAR") == 0);
  CHECK(sppc_getenv("SPPC_TEST_VAR", buf, sizeof buf) == SPPC_ENOENT && buf[0] == '\0');

  CHECK(sppc_setenv("SPPC_TEST_VAR", "first", false) == 0);
  CHECK(sppc_getenv("SPPC_TEST_VAR", buf, sizeof buf) == 0 && strcmp(buf, "first") == 0);
  // Without overwrite an existing value stays; with it, it is replaced.
  CHECK(sppc_setenv("SPPC_TEST_VAR", "second", false) == 0);
  CHECK(sppc_getenv("SPPC_TEST_VAR", buf, sizeof buf) == 0 && strcmp(buf, "first") == 0);
  CHECK(sppc_setenv("SPPC_TEST_VAR", "second", true) == 0);
  CHECK(sppc_getenv("SPPC_TEST_VAR", buf, sizeof buf) == 0 && strcmp(buf, "second") == 0);

  // The value and its terminator must fit; if not, nothing is written.
  CHECK(sppc_getenv("SPPC_TEST_VAR", buf, 7) == 0);
  CHECK(sppc_getenv("SPPC_TEST_VAR", buf, 6) == SPPC_ERANGE && buf[0] == '\0');
  CHECK(sppc_getenv("SPPC_TEST_VAR", buf, 0) == SPPC_ERANGE);

  // An empty value is still a value.
  CHECK(sppc_setenv("SPPC_TEST_VAR", "", true) == 0);
  CHECK(sppc_getenv("SPPC_TEST_VAR", buf, sizeof buf) == 0 && buf[0] == '\0');

  CHECK(sppc_unsetenv("SPPC_TEST_VAR") == 0);
  CHECK(sppc_getenv("SPPC_TEST_VAR", buf, sizeof buf) == SPPC_ENOENT);
  CHECK(sppc_unsetenv("SPPC_TEST_VAR") == 0); // already gone is fine

  // A name may not be empty or contain '='.
  CHECK(sppc_setenv("", "x", true) == SPPC_EINVAL);
  CHECK(sppc_setenv("A=B", "x", true) == SPPC_EINVAL);
  CHECK(sppc_unsetenv("A=B") == SPPC_EINVAL);
}

// ==================== IDS AND RANDOMNESS ====================

static void check_ids(void) {
  int32_t pid = 0, ppid = 0;
  sppc_get_pid(&pid);
  sppc_get_ppid(&ppid);
  CHECK(pid > 0 && pid == (int32_t)getpid());
  CHECK(ppid > 0 && ppid == (int32_t)getppid() && ppid != pid);

  // Signal 0 only asks whether the process is there.
  CHECK(sppc_signal(pid, 0) == 0);
  CHECK(sppc_signal(INT32_MAX, 0) == SPPC_ESRCH);
}

static void check_random(void) {
  char a[64], b[64];
  memset(a, 0, sizeof a);
  memset(b, 0, sizeof b);
  CHECK(sppc_getrandom(sizeof a, a) == 0);
  CHECK(sppc_getrandom(sizeof b, b) == 0);
  CHECK(memcmp(a, b, sizeof a) != 0);
  int zeros = 0;
  for (size_t i = 0; i < sizeof a; ++i) { zeros += a[i] == 0; }
  CHECK(zeros < 16); // 64 random bytes are not mostly zero

  // A large request is filled to the end, however many reads it takes.
  const size_t big = 1 << 20;
  unsigned char *p = sppc_calloc(big, 1);
  CHECK(p != NULL && sppc_getrandom(big, (char*)p) == 0);
  int tail_set = 0;
  for (size_t i = big - 64; p != NULL && i < big; ++i) { tail_set |= p[i]; }
  CHECK(tail_set != 0);
  sppc_free(p);
  CHECK(sppc_getrandom(0, a) == 0);
}

// ==================== STANDARD STREAMS ====================

/// Put a pipe in place of standard stream "fd" for the length of one call.
static int swap_in_pipe(const int fd, int *saved, int *other_end) {
  int r = -1, w = -1;
  if (sppc_pipe(&r, &w) != 0 || sppc_dup(fd, saved) != 0) { return -1; }
  if (fd == 0) {
    sppc_dup2(r, 0);
    sppc_close(r);
    *other_end = w;
  }
  else {
    sppc_dup2(w, fd);
    sppc_close(w);
    *other_end = r;
  }
  return 0;
}

static void restore(const int fd, const int saved) {
  sppc_dup2(saved, fd);
  sppc_close(saved);
}

static void check_streams(void) {
  int saved = -1, end = -1;
  ssize_t n = 0;
  char buf[32] = {0};

  fflush(stdout);
  CHECK(swap_in_pipe(1, &saved, &end) == 0);
  const int out_rc = sppc_stdout_write("to stdout", 1, 9, &n);
  restore(1, saved);
  CHECK(out_rc == 0 && n == 9);
  CHECK(sppc_read(buf, 1, sizeof buf, end, &n) == 0 && n == 9 && memcmp(buf, "to stdout", 9) == 0);
  sppc_close(end);

  CHECK(swap_in_pipe(2, &saved, &end) == 0);
  const int err_rc = sppc_stderr_write("to stderr", 3, 3, &n);
  restore(2, saved);
  CHECK(err_rc == 0 && n == 9);
  CHECK(sppc_read(buf, 1, sizeof buf, end, &n) == 0 && n == 9 && memcmp(buf, "to stderr", 9) == 0);
  sppc_close(end);

  CHECK(swap_in_pipe(0, &saved, &end) == 0);
  CHECK(sppc_write("from stdin", 1, 10, end, &n) == 0);
  sppc_close(end);
  int in_rc = sppc_stdin_read(buf, 1, sizeof buf, &n);
  CHECK(in_rc == 0 && n == 10 && memcmp(buf, "from stdin", 10) == 0);
  in_rc = sppc_stdin_read(buf, 1, sizeof buf, &n); // the writer is gone: end of input
  CHECK(in_rc == 0 && n == 0);
  restore(0, saved);
}

// ==================== ENDINGS ====================

static void say_atexit(void) {
  fputs("[atexit ran]", stderr);
}

static void unawaited_task(void *e) {
  (void)e;
  fputs("[task ran]", stderr);
}

static void ends_by_exit(void) {
  atexit(say_atexit);
  int h = -1;
  sppc_async(&h, (sppc_closure){unawaited_task, NULL});
  sppc_exit(3);
}

static void ends_by_halt(void) {
  atexit(say_atexit);
  sppc_halt(4);
}

// Called the way S++ calls them: through the library's exported symbol, a
// real call, rather than inlined into this file as a direct C call would be.
// The report starts from the return address into the caller, so an inlined
// copy would start one frame too far up.
static void (*volatile abort_fn)(void) = sppc_abort;
static void (*volatile check_failed_fn)(uint32_t, uint64_t, uint64_t) = sppc_check_failed;

// Not static and never inlined, so the backtrace has a name to show for it;
// the store after each call keeps it from becoming a tail jump, which would
// leave no frame of its own.
static volatile int after_call = 0;

_gnu_noinline void sppc_test_aborts_here(void) {
  abort_fn();
  after_call = 1;
}

_gnu_noinline void sppc_test_fails_check_here(void) {
  check_failed_fn(1, 5, 3); // index 5, length 3
  after_call = 1;
}

static uint32_t which_check;
static uint64_t check_a, check_b;

static void fails_a_check(void) {
  sppc_check_failed(which_check, check_a, check_b);
}

static void faults(void) {
  volatile int *volatile nowhere = NULL;
  *nowhere = 1;
}

static void raises_abort(void) {
  abort();
}

static void signals_itself(void) {
  int32_t pid = 0;
  sppc_get_pid(&pid);
  sppc_signal(pid, SPPC_SIGTERM);
  for (;;) { pause(); }
}

static void aborts_quietly(void) {
  // SPP_BACKTRACE=0 is read when the crash reporting is installed.
  sppc_setenv("SPP_BACKTRACE", "0", true);
  sppc_init();
  sppc_abort();
}

static char err[16384];

static void expect_check(const uint32_t check, const uint64_t a, const uint64_t b, char const *message) {
  which_check = check;
  check_a = a;
  check_b = b;
  CHECK(check_dies(fails_a_check, err, sizeof err) == 134);
  if (strstr(err, message) == NULL) {
    printf("FAIL: check %u printed:\n%s\n(wanted \"%s\")\n", check, err, message);
    ++check_failures;
  }
}

static void check_endings(void) {
  // exit: the status given; atexit handlers and tasks still runnable get to run.
  CHECK(check_dies(ends_by_exit, err, sizeof err) == 3);
  CHECK(strstr(err, "[task ran]") != NULL && strstr(err, "[atexit ran]") != NULL);

  // halt: the status given, and nothing else runs.
  CHECK(check_dies(ends_by_halt, err, sizeof err) == 4);
  CHECK(strstr(err, "[atexit ran]") == NULL);

  // abort: a report with a backtrace naming the function that aborted, and
  // the status a shell gives SIGABRT, without the signal itself.
  CHECK(check_dies(sppc_test_aborts_here, err, sizeof err) == 134);
  CHECK(strstr(err, "[sppc] aborted") != NULL);
  CHECK(strstr(err, "stack backtrace:") != NULL);
  CHECK(strstr(err, "#0  sppc_test_aborts_here+") != NULL);

  CHECK(check_dies(aborts_quietly, err, sizeof err) == 134);
  CHECK(strstr(err, "[sppc] aborted") != NULL && strstr(err, "stack backtrace:") == NULL);

  // A failed check: its message with the values, then the backtrace from
  // the code that failed.
  CHECK(check_dies(sppc_test_fails_check_here, err, sizeof err) == 134);
  CHECK(strstr(err, "check failed: index 5 out of bounds for length 3") != NULL);
  CHECK(strstr(err, "#0  sppc_test_fails_check_here+") != NULL);

  expect_check(2, 0, 0, "check failed: integer conversion");
  expect_check(3, 0, 0, "check failed: generator 'send'");
  expect_check(4, 7, 2, "check failed: slice 7..2 ends before it starts");
  expect_check(5, 9, 4, "check failed: slice end 9 out of bounds for length 4");
  expect_check(0x10, 0, 0, "check failed: signed add overflowed");
  expect_check(0x11, 0, 0, "check failed: unsigned add overflowed");
  expect_check(0x12, 0, 0, "check failed: signed subtract overflowed");
  expect_check(0x13, 0, 0, "check failed: unsigned subtract underflowed");
  expect_check(0x14, 0, 0, "check failed: signed multiply overflowed");
  expect_check(0x15, 0, 0, "check failed: unsigned multiply overflowed");
  expect_check(0x99, 0, 0, "check failed: unknown check 153");
  expect_check(1, UINT64_MAX, 0, "index 18446744073709551615 out of bounds for length 0");

  // Fatal signals: reported, then the signal's own default action.
  CHECK(check_dies(faults, err, sizeof err) == 128 + SIGSEGV);
  CHECK(strstr(err, "fatal signal: SIGSEGV") != NULL && strstr(err, "at address 0x0") != NULL);
  CHECK(strstr(err, "#0  faults+") != NULL);
  CHECK(check_dies(raises_abort, err, sizeof err) == 128 + SIGABRT);
  CHECK(strstr(err, "fatal signal: SIGABRT") != NULL);

  // A signal sppc does not report ends the process with no report.
  CHECK(check_dies(signals_itself, err, sizeof err) == 128 + SIGTERM);
  CHECK(strstr(err, "[sppc]") == NULL);
}

// ==================== TASK STACKS ====================

static int big_frame_done = 0;

static void uses_a_big_frame(void *e) {
  (void)e;
  volatile char frame[512 * 1024];
  frame[0] = 1;
  frame[sizeof frame - 1] = 1;
  big_frame_done = frame[0] + frame[sizeof frame - 1];
}

static void check_task_stack_size(void) {
  // Tasks spawned after the size is set get stacks that big.
  sppc_async_stack_size(2 * 1024 * 1024);
  int h = -1;
  CHECK(sppc_async(&h, (sppc_closure){uses_a_big_frame, NULL}) == 0);
  CHECK(sppc_await(h) == 0);
  CHECK(big_frame_done == 2);
}

int main(void) {
  if (sppc_init() != 0) { return 1; }
  check_environment();
  check_ids();
  check_random();
  check_streams();
  check_endings();
  check_task_stack_size();
  sppc_cleanup();
  return check_done();
}
