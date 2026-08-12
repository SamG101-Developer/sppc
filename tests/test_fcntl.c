// Regression test: sppc_fcntl must pass the third argument the command wants.
//
// It read `va_arg(ap, void*)` for every command, so F_GETFL and friends
// fetched a vararg that was never passed, and F_SETFL reinterpreted an int as
// a pointer. On x86-64 the register happened to carry the value through, but
// the upper bits were whatever was left in it.

#include <sppc/sppc.h>
#include "harness.h"

int main(void) {
  TEST_BEGIN("fcntl");
  sppc_init();
  char detail[128];

  char path[] = "/tmp/sppc_test_fcntl_XXXXXX";
  int fd = -1;
  CHECK_EQ("mktemp", sppc_mktemp(path, &fd), 0);

  // --- no-argument command ---
  CHECK_EQ("F_GETFL succeeds", sppc_fcntl(fd, F_GETFL), 0);
  const int flags = fcntl(fd, F_GETFL);
  snprintf(detail, sizeof detail, "flags=%#x", flags);
  CHECK("  flags look sane", flags >= 0 && (flags & O_ACCMODE) == O_RDWR, detail);

  CHECK_EQ("F_GETFD succeeds", sppc_fcntl(fd, F_GETFD), 0);

  // --- int-argument command, and the value must actually land ---
  CHECK_EQ("F_SETFL O_NONBLOCK", sppc_fcntl(fd, F_SETFL, flags | O_NONBLOCK), 0);
  const int after = fcntl(fd, F_GETFL);
  snprintf(detail, sizeof detail, "before=%#x after=%#x", flags, after);
  CHECK("  O_NONBLOCK is set", (after & O_NONBLOCK) != 0, detail);

  CHECK_EQ("F_SETFL clear O_NONBLOCK", sppc_fcntl(fd, F_SETFL, flags), 0);
  const int cleared = fcntl(fd, F_GETFL);
  snprintf(detail, sizeof detail, "flags=%#x", cleared);
  CHECK("  O_NONBLOCK is cleared", (cleared & O_NONBLOCK) == 0, detail);

  CHECK_EQ("F_SETFD FD_CLOEXEC", sppc_fcntl(fd, F_SETFD, FD_CLOEXEC), 0);
  snprintf(detail, sizeof detail, "fd flags=%#x", fcntl(fd, F_GETFD));
  CHECK("  FD_CLOEXEC is set", (fcntl(fd, F_GETFD) & FD_CLOEXEC) != 0, detail);

  // --- F_DUPFD takes an int and returns a new descriptor ---
  CHECK_EQ("F_DUPFD succeeds", sppc_fcntl(fd, F_DUPFD, 100), 0);
  const int dup_fd = fcntl(fd, F_DUPFD, 100);
  snprintf(detail, sizeof detail, "dup fd=%d", dup_fd);
  CHECK("  duplicate is >= 100", dup_fd >= 100, detail);
  if (dup_fd >= 0) close(dup_fd);

  // --- pointer-argument command ---
  struct flock lk = { .l_type = F_WRLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = 0 };
  CHECK_EQ("F_SETLK takes a pointer", sppc_fcntl(fd, F_SETLK, &lk), 0);

  struct flock probe = { .l_type = F_WRLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = 0 };
  CHECK_EQ("F_GETLK takes a pointer", sppc_fcntl(fd, F_GETLK, &probe), 0);
  snprintf(detail, sizeof detail, "l_type=%d F_UNLCK=%d", probe.l_type, F_UNLCK);
  CHECK("  lock is ours, so reported unlocked", probe.l_type == F_UNLCK, detail);

  lk.l_type = F_UNLCK;
  CHECK_EQ("F_SETLK unlock", sppc_fcntl(fd, F_SETLK, &lk), 0);

  // --- errors still propagate ---
  const int rc_bad = sppc_fcntl(-1, F_GETFL);
  snprintf(detail, sizeof detail, "rc=%d EBADF=%d", rc_bad, EBADF);
  CHECK("bad fd reports EBADF", rc_bad == EBADF, detail);

  close(fd);
  unlink(path);
  return TEST_SUMMARY();
}