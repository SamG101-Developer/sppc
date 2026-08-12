// Regression test: wrappers must report the right thing.
//
// Three families were wrong:
//   - calls returning char* used _return_normalized_err, which tests `err < 0`.
//     That is always false for a pointer, so failures reported success.
//   - clock_nanosleep returns its error number directly (positive), so the
//     same `err < 0` test never fired.
//   - the comparison wrappers treated a "less than" result as an error and
//     returned whatever errno happened to hold.
//
// errno is poisoned before the comparison checks: a regression surfaces as
// 12345 rather than a plausible-looking code.

#include <sppc/sppc.h>
#include "harness.h"

#define POISON 12345

int main(void) {
  TEST_BEGIN("error reporting");
  sppc_init();
  char detail[128];

  // --- pointer-returning calls must report failure ---
  char tiny[2];
  errno = 0;
  const int rc_cwd = sppc_getcwd(tiny, sizeof tiny);
  snprintf(detail, sizeof detail, "rc=%d ERANGE=%d", rc_cwd, ERANGE);
  CHECK("getcwd reports ERANGE on a small buffer", rc_cwd == ERANGE, detail);

  char big[4096];
  CHECK_EQ("getcwd succeeds with room", sppc_getcwd(big, sizeof big), 0);

  char resolved[4096];
  errno = 0;
  const int rc_rp = sppc_realpath("/definitely/does/not/exist/anywhere", resolved);
  snprintf(detail, sizeof detail, "rc=%d ENOENT=%d", rc_rp, ENOENT);
  CHECK("realpath reports ENOENT", rc_rp == ENOENT, detail);
  CHECK_EQ("realpath succeeds on /", sppc_realpath("/", resolved), 0);

  char bad_tmpl[] = "/nonexistent-dir/sppc_XXXXXX";
  errno = 0;
  const int rc_md = sppc_mktemp_dir(bad_tmpl);
  snprintf(detail, sizeof detail, "rc=%d ENOENT=%d", rc_md, ENOENT);
  CHECK("mktemp_dir reports ENOENT", rc_md == ENOENT, detail);

  char good_tmpl[] = "/tmp/sppc_test_dir_XXXXXX";
  const int rc_md_ok = sppc_mktemp_dir(good_tmpl);
  CHECK_EQ("mktemp_dir succeeds in /tmp", rc_md_ok, 0);
  if (rc_md_ok == 0) rmdir(good_tmpl);

  // --- clock_nanosleep returns its code directly ---
  struct timespec bad = { .tv_sec = 0, .tv_nsec = 2000000000L };  // out of range
  errno = 0;
  const int rc_ns = sppc_clock_nanosleep(CLOCK_MONOTONIC, 0, &bad);
  snprintf(detail, sizeof detail, "rc=%d EINVAL=%d", rc_ns, EINVAL);
  CHECK("clock_nanosleep reports EINVAL", rc_ns == EINVAL, detail);

  struct timespec ok = { .tv_sec = 0, .tv_nsec = 1000000L };      // 1ms
  CHECK_EQ("clock_nanosleep succeeds", sppc_clock_nanosleep(CLOCK_MONOTONIC, 0, &ok), 0);

  // --- comparisons: an ordering is not an error ---
  bool eq = true;
  errno = POISON;
  const int rc_lt = sppc_strcmp("apple", "banana", &eq);
  snprintf(detail, sizeof detail, "rc=%d eq=%d", rc_lt, (int)eq);
  CHECK("strcmp(a < b) succeeds", rc_lt == 0 && eq == false, detail);

  errno = POISON;
  const int rc_gt = sppc_strcmp("banana", "apple", &eq);
  CHECK("strcmp(a > b) succeeds", rc_gt == 0 && eq == false, "rc and eq");

  errno = POISON;
  const int rc_seq = sppc_strcmp("same", "same", &eq);
  CHECK("strcmp(equal) succeeds", rc_seq == 0 && eq == true, "rc and eq");

  errno = POISON;
  const int rc_ci = sppc_strcasecmp("ABC", "abd", &eq);
  snprintf(detail, sizeof detail, "rc=%d eq=%d", rc_ci, (int)eq);
  CHECK("strcasecmp(a < b) succeeds", rc_ci == 0 && eq == false, detail);

  errno = POISON;
  const int rc_ci_eq = sppc_strcasecmp("ABC", "abc", &eq);
  CHECK("strcasecmp(equal) succeeds", rc_ci_eq == 0 && eq == true, "rc and eq");

  // --- memcmp returns a sign, and never an error ---
  int sign = 99;
  errno = POISON;
  const int rc_lt_m = sppc_memcmp("aaa", "aab", 3, &sign);
  snprintf(detail, sizeof detail, "rc=%d sign=%d", rc_lt_m, sign);
  CHECK("memcmp(a < b) -> rc 0, sign -1", rc_lt_m == 0 && sign == -1, detail);

  errno = POISON;
  const int rc_gt_m = sppc_memcmp("aab", "aaa", 3, &sign);
  snprintf(detail, sizeof detail, "rc=%d sign=%d", rc_gt_m, sign);
  CHECK("memcmp(a > b) -> rc 0, sign 1", rc_gt_m == 0 && sign == 1, detail);

  errno = POISON;
  const int rc_eq_m = sppc_memcmp("aaa", "aaa", 3, &sign);
  snprintf(detail, sizeof detail, "rc=%d sign=%d", rc_eq_m, sign);
  CHECK("memcmp(equal) -> rc 0, sign 0", rc_eq_m == 0 && sign == 0, detail);

  // --- strcat cannot fail ---
  char cat[32] = "foo";
  errno = POISON;
  const int rc_cat = sppc_strcat(cat, "bar");
  snprintf(detail, sizeof detail, "rc=%d buf=\"%s\"", rc_cat, cat);
  CHECK("strcat succeeds", rc_cat == 0 && strcmp(cat, "foobar") == 0, detail);

  return TEST_SUMMARY();
}