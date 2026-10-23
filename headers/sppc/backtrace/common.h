#pragma once

/// The parts of crash reporting that do not depend on the platform: the
/// report buffer and the async-signal-safe formatting that fills it.

#include <sppc/config.h>
#include <sppc/macros.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

// Defined once, in sppc.c: a header-level "static" would give every file that
// includes this its own copy of the state, and an inline function may not
// refer to one at all.
extern bool _sppc_bt_enabled;
extern char const *_sppc_bt_off_note;
extern _sppc_atomic_flag _sppc_bt_busy;

// ==================== OUTPUT ====================

// The report is built up here and written in one call, so that other threads
// still printing cannot land between its pieces. Only the first fatal signal
// reports ("_sppc_bt_busy"), so one static buffer is enough; a report longer
// than it is cut off rather than split.
#define _SPPC_BT_BUF_SIZE (64 * 1024)
extern char _sppc_bt_buf[_SPPC_BT_BUF_SIZE];
extern size_t _sppc_bt_len;

_gnu_cold
inline void _sppc_bt_put_n(char const *s, size_t n) {
  const size_t room = sizeof _sppc_bt_buf - _sppc_bt_len;
  const size_t take = n < room ? n : room;
  memcpy(_sppc_bt_buf + _sppc_bt_len, s, take);
  _sppc_bt_len += take;
}

_gnu_cold
inline void _sppc_bt_put(char const *s) {
  _sppc_bt_put_n(s, strlen(s));
}

/// An s++ symbol ends in "$h" and a 16 hex digit hash of its signature, which
/// keeps overloads apart but is noise in a backtrace. Anything else is printed
/// whole.
_gnu_cold
inline void _sppc_bt_put_symbol(char const *name) {
  const size_t n = strlen(name);
  if (n >= 18 && name[n - 18] == '$' && name[n - 17] == 'h') {
    bool all_hex = true;
    for (size_t i = n - 16; i < n; ++i) {
      const char c = name[i];
      all_hex = all_hex && ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }
    if (all_hex) {
      _sppc_bt_put_n(name, n - 18);
      return;
    }
  }
  _sppc_bt_put(name);
}

_gnu_cold
inline void _sppc_bt_flush(void) {
  size_t done = (size_t)0;
  while (done < _sppc_bt_len) {
    const ssize_t w = write(STDERR_FILENO, _sppc_bt_buf + done, _sppc_bt_len - done);
    if (w < 0 && errno == EINTR) { continue; }
    if (w <= 0) { break; }
    done += (size_t)w;
  }
  _sppc_bt_len = 0;
}

_gnu_cold
inline void _sppc_bt_put_hex(uintptr_t v) {
  char buf[2 + 2 * sizeof v + 1];
  size_t i = sizeof buf - 1;
  buf[i] = '\0';
  do {
    buf[--i] = "0123456789abcdef"[v & 0xf];
    v >>= 4;
  }
  while (v != 0);
  buf[--i] = 'x';
  buf[--i] = '0';
  _sppc_bt_put(buf + i);
}

_gnu_cold
inline void _sppc_bt_put_dec(uint64_t v) {
  char buf[21];
  size_t i = sizeof buf - 1;
  buf[i] = '\0';
  do {
    buf[--i] = (char)('0' + v % 10);
    v /= 10;
  }
  while (v != 0);
  _sppc_bt_put(buf + i);
}
