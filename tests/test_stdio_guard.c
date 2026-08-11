// Regression test: the stdio wrappers must release their mutex on failure.
//
// They used to return early when the underlying read/write failed, leaving the
// mutex held forever, so the next call to that stream deadlocked. Without the
// fix this test HANGS rather than failing, which is why ctest gives it a
// timeout.

#include <sppc/sppc.h>
#include "harness.h"

int main(void) {
  TEST_BEGIN("stdio mutex guard");
  sppc_init();

  const int saved = dup(STDOUT_FILENO);
  ssize_t n = 0;
  char detail[64];

  // Closing stdout makes the inner write fail with EBADF.
  close(STDOUT_FILENO);
  const int rc1 = sppc_stdout_write("x", 1, 1, &n);
  const int held1 = pthread_mutex_trylock(&_stdout_mutex);
  if (held1 == 0) pthread_mutex_unlock(&_stdout_mutex);

  // The second call is the one that used to block forever.
  const int rc2 = sppc_stdout_write("y", 1, 1, &n);
  const int held2 = pthread_mutex_trylock(&_stdout_mutex);
  if (held2 == 0) pthread_mutex_unlock(&_stdout_mutex);

  dup2(saved, STDOUT_FILENO);
  close(saved);

  snprintf(detail, sizeof detail, "rc=%d EBADF=%d", rc1, EBADF);
  CHECK("failing write returns the real error", rc1 == EBADF, detail);
  CHECK("mutex released after failure", held1 == 0, held1 == EBUSY ? "still held" : "released");
  snprintf(detail, sizeof detail, "rc=%d", rc2);
  CHECK("second failing call does not deadlock", rc2 == EBADF, detail);
  CHECK("mutex released after 2nd failure", held2 == 0, held2 == EBUSY ? "still held" : "released");

  // Normal operation is unaffected.
  n = 0;
  const int rc3 = sppc_stderr_write("", 1, 0, &n);
  snprintf(detail, sizeof detail, "rc=%d n=%zd", rc3, n);
  CHECK("zero-length stderr write succeeds", rc3 == 0 && n == 0, detail);

  int tmp_fd = -1;
  char path[] = "/tmp/sppc_test_stdio_XXXXXX";
  sppc_mktemp(path, &tmp_fd);
  const int saved_out = dup(STDOUT_FILENO);
  dup2(tmp_fd, STDOUT_FILENO);
  n = 0;
  const int rc4 = sppc_stdout_write("hello\n", 1, 6, &n);
  dup2(saved_out, STDOUT_FILENO);
  close(saved_out);
  close(tmp_fd);
  unlink(path);
  snprintf(detail, sizeof detail, "rc=%d n=%zd", rc4, n);
  CHECK("normal write still works", rc4 == 0 && n == 6, detail);

  const int held3 = pthread_mutex_trylock(&_stdout_mutex);
  if (held3 == 0) pthread_mutex_unlock(&_stdout_mutex);
  CHECK("mutex released after success", held3 == 0, held3 == EBUSY ? "still held" : "released");

  return TEST_SUMMARY();
}