// Regression test: sppc_getenv must copy the whole value.
//
// It used to copy exactly one byte (`*out = *err`), leave no NUL terminator,
// and take no buffer size at all, so there was no safe way to use it. An unset
// variable also returned a stale errno rather than a defined code.

#include <sppc/sppc.h>
#include "harness.h"

int main(void) {
  TEST_BEGIN("getenv");
  sppc_init();
  char detail[128];
  char buf[64];

  setenv("SPPC_TEST_VALUE", "HELLO_WORLD", 1);
  unsetenv("SPPC_TEST_MISSING");

  // --- whole value, NUL terminated ---
  memset(buf, '#', sizeof buf);
  const int rc = sppc_getenv("SPPC_TEST_VALUE", buf, sizeof buf);
  snprintf(detail, sizeof detail, "rc=%d buf=\"%.20s\"", rc, buf);
  CHECK("reads the whole value", rc == 0 && strcmp(buf, "HELLO_WORLD") == 0, detail);
  CHECK("NUL terminated", buf[11] == '\0', buf[11] == '\0' ? "" : "no terminator");
  CHECK("does not write past the value", buf[12] == '#', "rest of buffer untouched");

  // --- unset variable is a defined error, not a stale errno ---
  errno = 12345;
  memset(buf, '#', sizeof buf);
  const int rc_missing = sppc_getenv("SPPC_TEST_MISSING", buf, sizeof buf);
  snprintf(detail, sizeof detail, "rc=%d ENOENT=%d", rc_missing, ENOENT);
  CHECK("unset variable returns ENOENT", rc_missing == ENOENT, detail);
  CHECK("  and empties the buffer", buf[0] == '\0', "out[0] == 0");

  // --- truncation is reported, not silent ---
  setenv("SPPC_TEST_LONG", "0123456789ABCDEF", 1);
  char small[8];
  memset(small, '#', sizeof small);
  const int rc_small = sppc_getenv("SPPC_TEST_LONG", small, sizeof small);
  snprintf(detail, sizeof detail, "rc=%d ERANGE=%d", rc_small, ERANGE);
  CHECK("too-small buffer returns ERANGE", rc_small == ERANGE, detail);
  CHECK("  and does not partially fill", small[0] == '\0', "out[0] == 0");

  // Exact fit: 16 chars + NUL needs 17.
  char exact[17];
  const int rc_exact = sppc_getenv("SPPC_TEST_LONG", exact, sizeof exact);
  snprintf(detail, sizeof detail, "rc=%d buf=\"%s\"", rc_exact, exact);
  CHECK("exact-fit buffer succeeds", rc_exact == 0 && strcmp(exact, "0123456789ABCDEF") == 0, detail);

  char off_by_one[16];
  const int rc_obo = sppc_getenv("SPPC_TEST_LONG", off_by_one, sizeof off_by_one);
  snprintf(detail, sizeof detail, "rc=%d (16 bytes cannot hold 16 chars + NUL)", rc_obo);
  CHECK("one byte short returns ERANGE", rc_obo == ERANGE, detail);

  const int rc_zero = sppc_getenv("SPPC_TEST_VALUE", buf, 0);
  snprintf(detail, sizeof detail, "rc=%d", rc_zero);
  CHECK("zero-size buffer returns ERANGE", rc_zero == ERANGE, detail);

  // --- empty value is a value, not an absence ---
  setenv("SPPC_TEST_EMPTY", "", 1);
  memset(buf, '#', sizeof buf);
  const int rc_empty = sppc_getenv("SPPC_TEST_EMPTY", buf, sizeof buf);
  snprintf(detail, sizeof detail, "rc=%d", rc_empty);
  CHECK("empty value succeeds", rc_empty == 0 && buf[0] == '\0', detail);

  // --- setenv/unsetenv round-trip through the wrappers ---
  CHECK_EQ("sppc_setenv", sppc_setenv("SPPC_TEST_RT", "round-trip", true), 0);
  memset(buf, '#', sizeof buf);
  CHECK_EQ("  reads back", sppc_getenv("SPPC_TEST_RT", buf, sizeof buf), 0);
  CHECK("  value matches", strcmp(buf, "round-trip") == 0, buf);
  CHECK_EQ("sppc_unsetenv", sppc_unsetenv("SPPC_TEST_RT"), 0);
  CHECK_EQ("  now ENOENT", sppc_getenv("SPPC_TEST_RT", buf, sizeof buf), ENOENT);

  return TEST_SUMMARY();
}