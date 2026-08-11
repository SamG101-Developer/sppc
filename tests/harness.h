#pragma once
#include <stdio.h>
#include <string.h>

// Minimal check harness. Each test file defines main(), runs CHECK/CHECK_EQ
// and ends with TEST_SUMMARY(), which is also the process exit status so
// ctest picks up failures.

static int _checks = 0;
static int _fails = 0;

#define CHECK(label, cond, detail)                                         \
  do {                                                                     \
    const int ok_ = (cond) ? 1 : 0;                                        \
    _checks++;                                                             \
    if (!ok_) _fails++;                                                    \
    printf("  %-46s %s  %s\n", (label), ok_ ? "PASS" : "FAIL", (detail));  \
    fflush(stdout);                                                        \
  } while (0)

#define CHECK_EQ(label, got, want)                                         \
  do {                                                                     \
    const long g_ = (long)(got), w_ = (long)(want);                        \
    char d_[64];                                                           \
    snprintf(d_, sizeof d_, "got=%ld want=%ld", g_, w_);                   \
    CHECK(label, g_ == w_, d_);                                            \
  } while (0)

#define TEST_BEGIN(name) printf("[%s]\n", (name))

#define TEST_SUMMARY()                                                     \
  (printf("  -> %d/%d passed%s\n\n", _checks - _fails, _checks,            \
          _fails ? "  *** FAILURES ***" : ""),                             \
   _fails != 0)