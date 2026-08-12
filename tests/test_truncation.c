// Regression test: wrappers must not discard what the syscall reported.
//
//   - readlink neither NUL-terminates nor reports truncation, and its return
//     value was the only record of the length. The wrapper threw it away, so
//     the caller could not tell where the path ended.
//   - getrandom may return fewer bytes than asked for; discarding that count
//     left the tail of the buffer unfilled while reporting success.
//   - set/getsockopt sized the option by sizeof(pointer) rather than
//     sizeof(value), an 8-byte read of (or write to) a 4-byte int.
//   - sendfile and copy_file_range report a byte count that a partial
//     transfer depends on; both were discarded.

#include <sppc/sppc.h>
#include "harness.h"
#include <netinet/in.h>

int main(void) {
  TEST_BEGIN("discarded results");
  sppc_init();
  char detail[256];

  // --- readlink terminates and bounds ---
  const char *target = "/etc/hostname";               // 13 chars
  char link_path[] = "/tmp/sppc_test_link_XXXXXX";
  close(mkstemp(link_path));
  unlink(link_path);
  symlink(target, link_path);

  char buf[64];
  memset(buf, 'Z', sizeof buf);
  const int rc = sppc_readlink(link_path, buf, sizeof buf);
  snprintf(detail, sizeof detail, "rc=%d buf=\"%.20s\"", rc, buf);
  CHECK("readlink NUL-terminates", rc == 0 && strcmp(buf, target) == 0, detail);
  CHECK("  does not write past the target", buf[14] == 'Z', "byte after NUL untouched");

  char exact[14];                                     // 13 chars + NUL
  memset(exact, 'Z', sizeof exact);
  const int rc_exact = sppc_readlink(link_path, exact, sizeof exact);
  snprintf(detail, sizeof detail, "rc=%d buf=\"%s\"", rc_exact, exact);
  CHECK("exact-fit buffer succeeds", rc_exact == 0 && strcmp(exact, target) == 0, detail);

  char tight[13];                                     // no room to terminate
  const int rc_tight = sppc_readlink(link_path, tight, sizeof tight);
  snprintf(detail, sizeof detail, "rc=%d ERANGE=%d", rc_tight, ERANGE);
  CHECK("no room for NUL returns ERANGE", rc_tight == ERANGE, detail);

  const int rc_zero = sppc_readlink(link_path, buf, 0);
  CHECK("zero-size buffer returns ERANGE", rc_zero == ERANGE, "rc");

  const int rc_missing = sppc_readlink("/no/such/link/here", buf, sizeof buf);
  snprintf(detail, sizeof detail, "rc=%d ENOENT=%d", rc_missing, ENOENT);
  CHECK("missing link reports ENOENT", rc_missing == ENOENT, detail);
  unlink(link_path);

  // --- getrandom fills the whole buffer ---
  // A large request is the one most likely to come back short.
  static char rnd[1 << 16];
  memset(rnd, 0, sizeof rnd);
  const int rc_rnd = sppc_getrandom(sizeof rnd, rnd);
  size_t zero_run = 0, longest = 0;
  for (size_t i = 0; i < sizeof rnd; i++) {
    if (rnd[i] == 0) { zero_run++; if (zero_run > longest) longest = zero_run; }
    else zero_run = 0;
  }
  snprintf(detail, sizeof detail, "rc=%d longest zero run=%zu of %zu bytes",
           rc_rnd, longest, sizeof rnd);
  // An unfilled tail shows up as a long run of zeroes; real randomness will
  // not produce anything close to 64 consecutive zero bytes.
  CHECK("getrandom fills the whole buffer", rc_rnd == 0 && longest < 64, detail);

  // --- socket options are sized by the value ---
  int fd = -1;
  CHECK_EQ("socket", sppc_socket(AF_INET, SOCK_STREAM, 0, &fd), 0);
  const int on = 1;
  CHECK_EQ("setsockopt(SO_REUSEADDR)", sppc_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on), 0);

  // Bracket the destination so an over-wide write would be visible.
  struct { int guard_lo; int value; int guard_hi; } probe = { 0x5A5A5A5A, 0, 0x5A5A5A5A };
  CHECK_EQ("getsockopt(SO_REUSEADDR)", sppc_getsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &probe.value), 0);
  snprintf(detail, sizeof detail, "value=%d lo=%#x hi=%#x", probe.value, probe.guard_lo, probe.guard_hi);
  CHECK("  reads back as set", probe.value != 0, detail);
  CHECK("  did not write past the int", probe.guard_hi == 0x5A5A5A5A, detail);
  close(fd);

  // --- sendfile reports its byte count ---
  char src_path[] = "/tmp/sppc_test_src_XXXXXX";
  int src = -1;
  CHECK_EQ("mktemp(src)", sppc_mktemp(src_path, &src), 0);
  ssize_t w = 0;
  sppc_write("0123456789", 1, 10, src, &w);
  lseek(src, 0, SEEK_SET);

  char dst_path[] = "/tmp/sppc_test_dst_XXXXXX";
  int dst = -1;
  CHECK_EQ("mktemp(dst)", sppc_mktemp(dst_path, &dst), 0);

  off_t off = 0;
  ssize_t sent = -1;
  const int rc_sf = sppc_sendfile(src, dst, &off, 10, &sent);
  snprintf(detail, sizeof detail, "rc=%d sent=%zd off=%lld", rc_sf, sent, (long long)off);
  CHECK("sendfile reports bytes sent", rc_sf == 0 && sent == 10, detail);
  CHECK("  and advances the offset", off == 10, detail);

  // --- copy_file_range reports its byte count ---
  char cp_path[] = "/tmp/sppc_test_cp_XXXXXX";
  int cp = -1;
  CHECK_EQ("mktemp(copy)", sppc_mktemp(cp_path, &cp), 0);
  lseek(src, 0, SEEK_SET);
  ssize_t copied = -1;
  const int rc_cp = sppc_copyfile(src, cp, 10, 0, &copied);
  snprintf(detail, sizeof detail, "rc=%d copied=%zd", rc_cp, copied);
  CHECK("copyfile reports bytes copied", rc_cp == 0 && copied == 10, detail);

  close(src); close(dst); close(cp);
  unlink(src_path); unlink(dst_path); unlink(cp_path);

  return TEST_SUMMARY();
}