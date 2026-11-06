#pragma once

/// The parts of crash reporting that do not depend on the platform: the
/// report buffer and the async-signal-safe formatting that fills it, the
/// backtrace printed from it, and the start of a failure's report. Windows'
/// needs only the stderr it is written to and the wait of a second failure
/// told apart. Each platform's format file (elf.h, macho.h, pe.h) supplies
/// what the backtrace stands on:
///
///   _sppc_bt_frames       the return addresses on the stack
///   _sppc_bt_resolve      a pc's function and object

#include <sppc/config.h>
#include <sppc/macros.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#if !SPPC_PLATFORM_WINDOWS
  #include <unistd.h>
#endif

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
#if SPPC_PLATFORM_WINDOWS
    // The standard error handle, which fd 2 sets (platform/windows.h).
    DWORD w = 0;
    if (!WriteFile(GetStdHandle(STD_ERROR_HANDLE), _sppc_bt_buf + done, (DWORD)(_sppc_bt_len - done), &w, NULL) || w == 0) { break; }
#else
    const ssize_t w = write(STDERR_FILENO, _sppc_bt_buf + done, _sppc_bt_len - done);
    if (w < 0 && errno == EINTR) { continue; }
    if (w <= 0) { break; }
#endif
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

// ==================== BACKTRACE ====================

#define _SPPC_BT_MAX_FRAMES 128

/// The return addresses on the stack, the first "max" of them: from the
/// fault's context "uctx", or from the caller where it is NULL.
inline size_t _sppc_bt_frames(void **frames, size_t max, void const *uctx);

/// The function "lookup_pc" is in, its start in "out_start", and the file of
/// the object it is in ("out_object"); NULL where there is no name for it.
inline char const* _sppc_bt_resolve(void const *lookup_pc, uintptr_t *out_start, char const **out_object);

/// Print the stack from @p first_pc down. @p uctx is the fault's context (a
/// signal's ucontext, a Windows exception's CONTEXT), and NULL for a failure
/// reported by call.
_gnu_cold
inline void _sppc_bt_print(void const *uctx, void const *first_pc) {
  // What the report says so far goes out first: the walk below is the one
  // part of it that can itself fault (an unwinder misreading a signal frame,
  // say), and a report cut off there still says what happened.
  _sppc_bt_flush();
  void *frames[_SPPC_BT_MAX_FRAMES];
  const size_t n = _sppc_bt_frames(frames, _SPPC_BT_MAX_FRAMES, uctx);
  const bool first_is_fault = uctx != NULL;

  // Start at "first_pc", dropping the reporter's own frames above it: for a
  // signal, the frame it interrupted, whose address is where the fault
  // happened; for a failure reported by call ("_sppc_bt_fail_end"), the
  // return address into the code that failed. Every return address can sit
  // just past the end of its function when the call was the last thing in it
  // (a call to something that does not return always is), so those are
  // looked up one byte back.
  // A thumb return address has its low bit set, where the unwinder's are
  // the instruction's own; a SPARC one is the call itself, where the
  // unwinder's is where it returns to, 8 bytes on. A fault's is exact.
#if SPPC_ARCH_ARM
  void const *const want = (void const*)((uintptr_t)first_pc & ~(uintptr_t)1);
#elif SPPC_ARCH_SPARC64
  void const *const want = first_is_fault ? first_pc : (void const*)((uintptr_t)first_pc + 8);
#else
  void const *const want = first_pc;
#endif
  size_t start = (size_t)0;
  bool found = false;
  for (size_t i = 0; i < n; ++i) {
    if (frames[i] == want) {
      start = i;
      found = true;
      break;
    }
  }

  // An unwinder that cannot step through the signal frame (qemu's ppc32
  // vdso describes it 16 bytes off) never reaches "want", and what it did
  // find is the reporter's own frames: "want" alone is the honest report.
  size_t count = n;
  if (!found && first_pc != NULL) {
    frames[0] = (void*)(uintptr_t)want;
    count = (size_t)1;
  }

  _sppc_bt_put("stack backtrace:\n");
  for (size_t i = start; i < count; ++i) {
    const uintptr_t pc = (uintptr_t)frames[i];
    const uintptr_t lookup = i == start && first_is_fault && first_pc != NULL ? pc : pc - 1;
    uintptr_t sym_start = (uintptr_t)0;
    char const *object = (char const*)NULL;
    char const *const name = _sppc_bt_resolve((void const*)lookup, &sym_start, &object);

    _sppc_bt_put("  #");
    _sppc_bt_put_dec(i - start);
    _sppc_bt_put(i - start < 10 ? "  " : " ");
    if (name != NULL) {
      _sppc_bt_put_symbol(name);
      _sppc_bt_put("+");
      _sppc_bt_put_hex(pc - sym_start);
    }
    else {
      _sppc_bt_put("?? ");
      _sppc_bt_put_hex(pc);
    }
    if (object != NULL && object[0] != '\0') {
      char const *slash = strrchr(object, '/');
#if SPPC_PLATFORM_WINDOWS
      char const *const back = strrchr(object, '\\');
      if (back != NULL && (slash == NULL || back > slash)) { slash = back; }
#endif
      _sppc_bt_put("  (");
      _sppc_bt_put(slash != NULL ? slash + 1 : object);
      _sppc_bt_put(")");
    }
    _sppc_bt_put("\n");
  }
}

// ==================== FAILURE ====================

/// The status a failure reported by call ends the process with: the one a
/// shell gives a process killed by "SIGABRT", so a script testing for an
/// abort by status still sees one.
#define _SPPC_FAIL_STATUS 134

/// Begin reporting a failure that no signal raised: a failed runtime check,
/// or "sppc_abort". The message goes into the report buffer between this and
/// "_sppc_bt_fail_end". As for a signal, only the first failure reports, and
/// any other thread arriving waits for it to end the process.
_gnu_cold
inline void _sppc_bt_fail_begin(void) {
  if (_sppc_atomic_flag_test_and_set(&_sppc_bt_busy)) {
#if SPPC_PLATFORM_WINDOWS
    for (;;) { Sleep(INFINITE); }
#else
    for (;;) { pause(); }
#endif
  }
}

/// Add a backtrace from "caller_pc", the return address into the code that
/// failed, write the report, and end the process with "_exit": no handler,
/// "atexit" function or stream flush runs, so none can be reached as a gadget
/// from a failure, and nothing is allocated or unwound.
_gnu_cold _gnu_noreturn
inline void _sppc_bt_fail_end(void const *caller_pc) {
  if (_sppc_bt_enabled) { _sppc_bt_print(NULL, caller_pc); }
  else { _sppc_bt_put(_sppc_bt_off_note); }
  _sppc_bt_flush();
  _exit(_SPPC_FAIL_STATUS);
}
