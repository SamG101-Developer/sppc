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
///
/// A frame's source lines come from a table the program carries, the same on
/// every platform (SOURCE LINES).

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
inline void _sppc_bt_put_n(char const *s, const size_t n) {
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

// ==================== SOURCE LINES ====================

// The program's source lines: a table the compiler builds from its debug info
// and links into the program ("[binary.debug] info" in spp), handed over by
// "sppc_set_source_lines" before the program starts. A frame is looked up by
// its function's symbol name, as "_sppc_bt_resolve" gives it, and its offset
// into it, so nothing in the table is an address and nothing relocates it.
//
// Version 1. Every field is in the target's byte order, and the table is
// 8-byte aligned. The compiler writes it (spp's "spp.codegen.llvm_lines"), so
// a change here is a new version there too.
//   header     the magic, the version, and the count of each array below
//   functions  one per function: the 64-bit FNV-1a hash of its symbol name,
//              and its ranges; sorted by hash
//   ranges     each function's, sorted by offset: where in it a range starts,
//              and the innermost frame of the code there, or none; a range
//              runs to the next, the last to the function's end
//   frames     a place in the source: its function's name, its file, line and
//              column, and the frame it was inlined into, or none; a chain
//              ends at the function the code is in
//   strings    NUL-terminated; names and files are offsets into them

#define _SPPC_LINES_MAGIC 0x4c505053u // "SPPL", read little-endian
#define _SPPC_LINES_VERSION 1u
#define _SPPC_LINES_NONE 0xffffffffu

// A chain longer than any inlining is a table gone wrong, not a deep one.
#define _SPPC_LINES_MAX_DEPTH 64

typedef struct {
  uint32_t magic;
  uint32_t version;
  uint32_t n_functions;
  uint32_t n_ranges;
  uint32_t n_frames;
  uint32_t strings_size;
} _sppc_lines_header;

typedef struct {
  uint64_t hash;
  uint32_t first_range;
  uint32_t n_ranges;
} _sppc_lines_function;

typedef struct {
  uint32_t offset;
  uint32_t frame;
} _sppc_lines_range;

typedef struct {
  uint32_t name;
  uint32_t file;
  uint32_t line;
  uint32_t column;
  uint32_t inlined_into;
} _sppc_lines_frame;

// Set once, before the program's own code runs ("sppc_set_source_lines").
extern void const *_sppc_bt_lines;

/// The 64-bit FNV-1a hash of a symbol name, as the compiler keys the table.
_gnu_cold
inline uint64_t _sppc_bt_lines_hash(char const *name) {
  uint64_t h = 0xcbf29ce484222325ull;
  for (; *name != '\0'; ++name) {
    h ^= (uint8_t)*name;
    h *= 0x100000001b3ull;
  }
  return h;
}

/// Put the table's string at "at", stopping at its NUL or at the end of the
/// strings, whichever comes first.
_gnu_cold
inline void _sppc_bt_put_lines_string(
  char const *strings, const uint32_t size, const uint32_t at) {
  uint32_t end = at;
  while (end < size && strings[end] != '\0') { ++end; }
  _sppc_bt_put_n(strings + at, (size_t)(end - at));
}

/// Print where in the source "offset" into the function named "name" is: a
/// line per frame, innermost first, down to the function's own. Nothing for a
/// function the table does not have, or with no table, or one of a version
/// this runtime does not read. Every index is checked before it is followed:
/// this runs in a crash, and must not make another.
_gnu_cold
inline void _sppc_bt_put_lines(char const *name, const uintptr_t offset) {
  _sppc_lines_header const *const h = (_sppc_lines_header const*)_sppc_bt_lines;
  if (h == NULL || h->magic != _SPPC_LINES_MAGIC || h->version != _SPPC_LINES_VERSION) { return; }
  _sppc_lines_function const *const functions = (_sppc_lines_function const*)(h + 1);
  _sppc_lines_range const *const ranges = (_sppc_lines_range const*)(functions + h->n_functions);
  _sppc_lines_frame const *const frames = (_sppc_lines_frame const*)(ranges + h->n_ranges);
  char const *const strings = (char const*)(frames + h->n_frames);

  // The function, by its name's hash.
  const uint64_t hash = _sppc_bt_lines_hash(name);
  uint32_t lo = 0, hi = h->n_functions;
  while (lo < hi) {
    const uint32_t mid = lo + (hi - lo) / 2;
    if (functions[mid].hash < hash) { lo = mid + 1; }
    else { hi = mid; }
  }
  if (lo == h->n_functions || functions[lo].hash != hash) { return; }
  _sppc_lines_function const *const fn = &functions[lo];
  if (fn->first_range > h->n_ranges || fn->n_ranges > h->n_ranges - fn->first_range) { return; }

  // The last range starting at or before the offset.
  _sppc_lines_range const *const own = ranges + fn->first_range;
  uint32_t r_lo = 0, r_hi = fn->n_ranges;
  while (r_lo < r_hi) {
    const uint32_t mid = r_lo + (r_hi - r_lo) / 2;
    if (own[mid].offset <= offset) { r_lo = mid + 1; }
    else { r_hi = mid; }
  }
  if (r_lo == 0) { return; }

  // The frames, innermost first. An inlined one names the function it is in,
  // which has no frame of its own on the stack; the last is the function the
  // frame's line above already named.
  uint32_t at = own[r_lo - 1].frame;
  for (int depth = 0; at < h->n_frames && depth < _SPPC_LINES_MAX_DEPTH; ++depth) {
    _sppc_lines_frame const *const f = &frames[at];
    if (f->file >= h->strings_size) { return; }
    _sppc_bt_put("          at ");
    _sppc_bt_put_lines_string(strings, h->strings_size, f->file);
    _sppc_bt_put(":");
    _sppc_bt_put_dec(f->line);
    if (f->column != 0) {
      _sppc_bt_put(":");
      _sppc_bt_put_dec(f->column);
    }
    if (f->inlined_into != _SPPC_LINES_NONE && f->name < h->strings_size) {
      _sppc_bt_put("  (inlined ");
      _sppc_bt_put_lines_string(strings, h->strings_size, f->name);
      _sppc_bt_put(")");
    }
    _sppc_bt_put("\n");
    at = f->inlined_into;
  }
}

/// Hand the runtime the program's source-line table (above), from which its
/// crash reports name each frame's file and line. The compiler's C entry
/// point calls it first, when the program has one. NULL, or a table of a
/// version this runtime does not read, leaves reports without lines.
_gnu_inline
_sppc_api void sppc_set_source_lines(void const *table) {
  _sppc_bt_lines = table;
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
inline void _sppc_bt_print(void const *uctx, void const *first_pc, const bool walk) {
  // What the report says so far goes out first: the walk below is the one
  // part of it that can itself fault (an unwinder misreading a signal frame,
  // say), and a report cut off there still says what happened. A stack whose
  // control flow is known to have gone wrong (a BTI fault, a failed return
  // address authentication) is not walked at all ("walk" false): an unwinder
  // that authenticates what it reads (libgcc on AArch64) would fault on it
  // again; only the frame that failed is reported.
  _sppc_bt_flush();
  void *frames[_SPPC_BT_MAX_FRAMES];
  const size_t n = walk ? _sppc_bt_frames(frames, _SPPC_BT_MAX_FRAMES, uctx) : (size_t)0;
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
    if (name != NULL) { _sppc_bt_put_lines(name, lookup - sym_start); }
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
  if (_sppc_bt_enabled) { _sppc_bt_print(NULL, caller_pc, true); }
  else { _sppc_bt_put(_sppc_bt_off_note); }
  _sppc_bt_flush();
  _exit(_SPPC_FAIL_STATUS);
}
