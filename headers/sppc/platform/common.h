#pragma once

/// The public api every platform implements the same way, over what its own
/// file (platform/posix.h, platform/windows.h) has defined by the time it
/// includes this, at its end: pthreads by their POSIX names (sppc's own on
/// Windows), the async runtime, "sppc_read" / "sppc_write", the split stack,
/// the crash reporter's per-thread hooks ("_sppc_bt_altstack_up" /
/// "_down"), the translation of a clock and a timespec ("_sppc_to_os_clock",
/// "_sppc_to_os_timespec"), "_sppc_memmem", "_sppc_strcasecmp" and
/// "_sppc_strcasestr", "sppc_malloc", "sppc_cleanup", and the crash
/// reporter's failure hooks ("_sppc_bt_fail_begin" / "_put" / "_fail_end").
///
/// The mem* and str* helpers; the ways a process ends (exit, halt, abort,
/// a failed runtime check); os threads, once, mutexes, condition variables, rwlocks
/// and spin locks, each lock taken through the async runtime so a task
/// waiting on it lets the rest of its thread run; barriers where pthreads
/// has them (Darwin builds its own); and the standard streams.

#include <sppc/config.h>
#include <sppc/macros.h>
#include <sppc/closure.h>
#include <sppc/types.h>
#include <sppc/errors.h>
#include <sppc/async.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// ==================== MEMORY ====================

/// Compare two memory regions.
_gnu_inline _gnu_hot _gnu_restrict_access(read_only, 1)
_gnu_restrict_access(read_only, 2) _gnu_restrict_access(write_only, 4)
_gnu_nonnull(1, 2, 4)
_sppc_api int sppc_memcmp(
  void const *ptr1, void const *ptr2, const size_t size,
  int *_sppc_restrict out) {
  _extract_err(int) memcmp(ptr1, ptr2, size);
  _sret_sign_store(out)
  return 0;
}

/// Find a needle in a haystack of memory.
_gnu_inline _gnu_restrict_access(read_only, 1)
_gnu_restrict_access(read_only, 3) _gnu_restrict_access(write_only, 5)
_gnu_nonnull(1, 3, 5)
_sppc_api int sppc_memmem(
  void const *haystack, const size_t haystack_size, void const *needle,
  const size_t needle_size, size_t *_sppc_restrict out) {
  // An empty needle is found at the start everywhere: Darwin's memmem, an old
  // FreeBSD one, finds it nowhere.
  if (needle_size == 0) {
    *out = 0;
    return 0;
  }
  _extract_err(void*) _sppc_memmem(
    haystack, haystack_size, needle, needle_size);
  if (err != NULL) { *out = (size_t)((char*)err - (char*)haystack); }
  _return_found
}

// ==================== STRINGS ====================

/// Find a needle in a haystack string, from the end.
_gnu_inline _gnu_restrict_access(read_only, 1)
_gnu_restrict_access(read_only, 2) _gnu_nonnull(1, 2)
char* _sppc_strrstr(
  const char *_sppc_restrict haystack, const char *_sppc_restrict needle) {
  if (*needle == '\0') { return (char*)haystack + strlen(haystack); }
  char *last = (char*)NULL;
  char const *p = haystack;

  while ((p = strstr(p, needle)) != NULL) {
    last = (char*)p;
    p++;
  }

  return last;
}

/// Copy a string region.
_gnu_inline _gnu_restrict_access(write_only, 1)
_gnu_restrict_access(read_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_strcpy(
  char *_sppc_restrict dest, char const *_sppc_restrict src) {
  _extract_err(char*) strcpy(dest, src);
  _return_normalized_ptr_err
}

/// Concatenate a source string to the destination string.
_gnu_inline _gnu_restrict_access(read_write, 1)
_gnu_restrict_access(read_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_strcat(
  char *_sppc_restrict dest, char const *_sppc_restrict src) {
  _extract_err(char*) strcat(dest, src);
  _return_normalized_ptr_err
}

/// Compare two strings.
_gnu_inline _gnu_restrict_access(read_only, 1)
_gnu_restrict_access(read_only, 2) _gnu_restrict_access(write_only, 3)
_gnu_nonnull(1, 2, 3)
_sppc_api int sppc_strcmp(
  char const *str1, char const *str2, bool *_sppc_restrict out) {
  _extract_err(int) strcmp(str1, str2);
  *out = err == 0 ? true : false;
  return 0;
}

/// Compare two strings, ignoring the case.
_gnu_inline _gnu_restrict_access(read_only, 1)
_gnu_restrict_access(read_only, 2) _gnu_restrict_access(write_only, 3)
_gnu_nonnull(1, 2, 3)
_sppc_api int sppc_strcasecmp(
  char const *str1, char const *str2, bool *_sppc_restrict out) {
  _extract_err(int) _sppc_strcasecmp(str1, str2);
  *out = err == 0 ? true : false;
  return 0;
}

/// Find a char in a string, from the beginning.
_gnu_inline _gnu_restrict_access(read_only, 1)
_gnu_restrict_access(write_only, 3) _gnu_nonnull(1, 3)
_sppc_api int sppc_strchr(
  char const *str, const char ch, size_t *_sppc_restrict out_idx) {
  _extract_err(char*) strchr(str, ch);
  if (err != NULL) { *out_idx = (size_t)(err - str); }
  _return_found
}

/// Find a char in a string, from the end.
_gnu_inline _gnu_restrict_access(read_only, 1)
_gnu_restrict_access(write_only, 3) _gnu_nonnull(1, 3)
_sppc_api int sppc_strrchr(
  char const *str, const char ch, size_t *_sppc_restrict out_idx) {
  _extract_err(char*) strrchr(str, ch);
  if (err != NULL) { *out_idx = (size_t)(err - str); }
  _return_found
}

/// Find a string needle in a string haystack, from the beginning.
_gnu_inline _gnu_restrict_access(read_only, 1)
_gnu_restrict_access(read_only, 2) _gnu_restrict_access(write_only, 3)
_gnu_nonnull(1, 2, 3)
_sppc_api int sppc_strstr(
  char const *haystack, char const *needle, size_t *_sppc_restrict out_idx) {
  _extract_err(char*) strstr(haystack, needle);
  if (err != NULL) { *out_idx = (size_t)(err - haystack); }
  _return_found
}

/// Find a string needle in a string haystack, from the end.
_gnu_inline _gnu_restrict_access(read_only, 1)
_gnu_restrict_access(read_only, 2) _gnu_restrict_access(write_only, 3)
_gnu_nonnull(1, 2, 3)
_sppc_api int sppc_strrstr(
  char const *haystack, char const *needle, size_t *_sppc_restrict out_idx) {
  _extract_err(char*) _sppc_strrstr(haystack, needle);
  if (err != NULL) { *out_idx = (size_t)(err - haystack); }
  _return_found
}

/// Find a string needle in a string haystack, from the beginning ignoring case.
_gnu_inline _gnu_restrict_access(read_only, 1)
_gnu_restrict_access(read_only, 2) _gnu_restrict_access(write_only, 3)
_gnu_nonnull(1, 2, 3)
_sppc_api int sppc_strcasestr(
  char const *haystack, char const *needle, size_t *_sppc_restrict out_idx) {
  _extract_err(char*) _sppc_strcasestr(haystack, needle);
  if (err != NULL) { *out_idx = (size_t)(err - haystack); }
  _return_found
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_gnu_restrict_access(read_only, 2) _gnu_restrict_access(write_only, 3)
_gnu_nonnull(1, 2, 3)
_sppc_api int sppc_strpbrk(
  char const *string, char const *accept, size_t *_sppc_restrict out_idx) {
  _extract_err(char*) strpbrk(string, accept);
  if (err != NULL) { *out_idx = (size_t)(err - string); }
  _return_found
}

_gnu_inline _gnu_malloc _gnu_restrict_access(read_only, 1) _gnu_nonnull(1)
_sppc_api void* sppc_strdup(char const *str) {
  // From sppc's own heap, so "sppc_free" takes it everywhere (on Windows a
  // block sppc frees carries a header of its own).
  const size_t len = strlen(str) + 1;
  char *const copy = (char*)sppc_malloc(len);
  if (copy != NULL) { memcpy(copy, str, len); }
  return copy;
}

// ==================== SOCKET ADDRESSES ====================

// An sppc_sockaddr_storage holds the platform's own address, which only
// these read or write (platform/shared.h for its view of one).

/// A port between host and network byte order, either way. Not "htons" /
/// "ntohs": Darwin defines those through a static function, which an inline
/// one with external linkage may not call (C17 6.7.4p3).
_gnu_inline
uint16_t _sppc_net16(const uint16_t v) {
#if SPPC_ENDIAN_BIG
  return v;
#else
  return (uint16_t)(v << 8 | v >> 8);
#endif
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(write_only, 3) _gnu_nonnull(1, 3)
_sppc_api void sppc_set_sockaddr_v4(uint8_t const *octets, const uint16_t port,
  sppc_sockaddr_storage *_sppc_restrict out_storage) {
  struct sockaddr_in *const addr = (struct sockaddr_in*)_sppc_os_sockaddr(out_storage);
  addr->sin_family = AF_INET;
  addr->sin_port = _sppc_net16(port);
  memset(&addr->sin_zero, 0, sizeof(addr->sin_zero));
  memcpy(&addr->sin_addr.s_addr, octets, 4);
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(write_only, 3) _gnu_nonnull(1, 3)
_sppc_api void sppc_set_sockaddr_v6(uint16_t const *segments, const uint16_t port,
  sppc_sockaddr_storage *_sppc_restrict out_storage) {
  struct sockaddr_in6 *const addr = (struct sockaddr_in6*)_sppc_os_sockaddr(out_storage);
  addr->sin6_family = AF_INET6;
  addr->sin6_port = _sppc_net16(port);
  addr->sin6_flowinfo = 0;
  addr->sin6_scope_id = 0;
  // Segments arrive in host order, as the port does, and go out big-endian.
  for (size_t i = 0; i < 8; ++i) {
    addr->sin6_addr.s6_addr[2 * i] = (uint8_t)(segments[i] >> 8);
    addr->sin6_addr.s6_addr[2 * i + 1] = (uint8_t)(segments[i] & 0xff);
  }
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(write_only, 2) _gnu_restrict_access(write_only, 3)
_gnu_nonnull(1, 2, 3)
_sppc_api void sppc_get_sockaddr_v4(sppc_sockaddr_storage const *_sppc_restrict storage, uint8_t *out_octets,
  uint16_t *out_port) {
  struct sockaddr_in const *const addr = (struct sockaddr_in const*)_sppc_os_sockaddr_const(storage);
  memcpy(out_octets, &addr->sin_addr.s_addr, 4);
  *out_port = _sppc_net16(addr->sin_port);
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(write_only, 2) _gnu_restrict_access(write_only, 3)
_gnu_nonnull(1, 2, 3)
_sppc_api void sppc_get_sockaddr_v6(sppc_sockaddr_storage const *_sppc_restrict storage, uint16_t *out_segments,
  uint16_t *out_port) {
  struct sockaddr_in6 const *const addr = (struct sockaddr_in6 const*)_sppc_os_sockaddr_const(storage);
  for (size_t i = 0; i < 8; ++i) {
    out_segments[i] = (uint16_t)(addr->sin6_addr.s6_addr[2 * i] << 8 | addr->sin6_addr.s6_addr[2 * i + 1]);
  }
  *out_port = _sppc_net16(addr->sin6_port);
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(write_only, 2) _gnu_nonnull(1, 2)
_sppc_api void sppc_sockaddr_family(sppc_sockaddr_storage const *_sppc_restrict storage, int *_sppc_restrict out_family) {
  size_t n;
  _sppc_pair const *const families = _sppc_af_table(&n);
  *out_family = _sppc_from_os_value(_sppc_os_sockaddr_const(storage)->ss_family, families, n, SPPC_AF_UNSPEC);
}

// ==================== PROCESSES ====================

_gnu_inline _gnu_noreturn _gnu_cold
_sppc_api void sppc_exit(const int status) {
  sppc_cleanup();
  exit(status);
}

_gnu_inline _gnu_noreturn _gnu_cold
_sppc_api void sppc_halt(const int status) {
  _exit(status);
}

/// End the process now, through the same path as a failed runtime check
/// ("sppc_check_failed"): a backtrace, then "_exit". Not libc's "abort",
/// whose "SIGABRT" runs whatever handler is installed for it.
_gnu_inline _gnu_noreturn _gnu_cold
_sppc_api void sppc_abort(void) {
  _sppc_bt_fail_begin();
  _sppc_bt_put("\n[sppc] aborted\n");
  _sppc_bt_fail_end(_sppc_return_address());
}

/// The runtime checks the compiler emits, each failing through
/// "sppc_check_failed". The compiler mirrors these numbers (RuntimeCheck in
/// llvm_fn_impls.cpp), so they are only ever added to.
enum {
  SPPC_CHECK_INDEX = 1, // a: the index, b: the length
  SPPC_CHECK_CONVERSION = 2,
  SPPC_CHECK_GENERATOR_SEND = 3,
  SPPC_CHECK_SLICE_ORDER = 4, // a: the start, b: the end
  SPPC_CHECK_SLICE_END = 5, // a: the end, b: the length
  SPPC_CHECK_FLOAT_TO_INT = 6, // a: the integer's width, b: 1 if it is signed
  SPPC_CHECK_FFI_UNWIND = 7, // a: the foreign function's name, a constant C string
  SPPC_CHECK_SIGNED_ADD = 0x10,
  SPPC_CHECK_UNSIGNED_ADD = 0x11,
  SPPC_CHECK_SIGNED_SUB = 0x12,
  SPPC_CHECK_UNSIGNED_SUB = 0x13,
  SPPC_CHECK_SIGNED_MUL = 0x14,
  SPPC_CHECK_UNSIGNED_MUL = 0x15,
  SPPC_CHECK_DIVIDE_BY_ZERO = 0x16,
  SPPC_CHECK_REMAINDER_BY_ZERO = 0x17,
  SPPC_CHECK_SIGNED_DIV = 0x18, // the minimum divided by -1; its remainder is 0, not a check
};

/// The one path a failed runtime check ends the process through: a fixed
/// message naming the check, with the values it was given, then a backtrace
/// and "_exit". Nothing allocates, and no code of the program's runs again.
_gnu_inline _gnu_noreturn _gnu_cold
_sppc_api void sppc_check_failed(
  const uint32_t check, const uint64_t a, const uint64_t b) {
  _sppc_bt_fail_begin();
  _sppc_bt_put("\n[sppc] check failed: ");
  switch (check) {
    case SPPC_CHECK_INDEX:
      _sppc_bt_put("index ");
      _sppc_bt_put_dec(a);
      _sppc_bt_put(" out of bounds for length ");
      _sppc_bt_put_dec(b);
      break;
    case SPPC_CHECK_CONVERSION:
      _sppc_bt_put("integer conversion reached for a width pair it is not defined for");
      break;
    case SPPC_CHECK_GENERATOR_SEND:
      _sppc_bt_put("generator 'send' was reached directly; it is lowered through the '.res()' operator");
      break;
    case SPPC_CHECK_SLICE_ORDER:
      _sppc_bt_put("slice ");
      _sppc_bt_put_dec(a);
      _sppc_bt_put("..");
      _sppc_bt_put_dec(b);
      _sppc_bt_put(" ends before it starts");
      break;
    case SPPC_CHECK_SLICE_END:
      _sppc_bt_put("slice end ");
      _sppc_bt_put_dec(a);
      _sppc_bt_put(" out of bounds for length ");
      _sppc_bt_put_dec(b);
      break;
    case SPPC_CHECK_FLOAT_TO_INT:
      _sppc_bt_put("float out of range for ");
      _sppc_bt_put(b != 0 ? "S" : "U");
      _sppc_bt_put_dec(a);
      _sppc_bt_put(", or NaN");
      break;
    case SPPC_CHECK_FFI_UNWIND:
      _sppc_bt_put("an exception unwound into s++ from '");
      _sppc_bt_put((char const*)(uintptr_t)a);
      _sppc_bt_put("'; foreign code must not unwind through s++ frames");
      break;
    case SPPC_CHECK_SIGNED_ADD:
      _sppc_bt_put("signed add overflowed");
      break;
    case SPPC_CHECK_UNSIGNED_ADD:
      _sppc_bt_put("unsigned add overflowed");
      break;
    case SPPC_CHECK_SIGNED_SUB:
      _sppc_bt_put("signed subtract overflowed");
      break;
    case SPPC_CHECK_UNSIGNED_SUB:
      _sppc_bt_put("unsigned subtract underflowed");
      break;
    case SPPC_CHECK_SIGNED_MUL:
      _sppc_bt_put("signed multiply overflowed");
      break;
    case SPPC_CHECK_UNSIGNED_MUL:
      _sppc_bt_put("unsigned multiply overflowed");
      break;
    case SPPC_CHECK_DIVIDE_BY_ZERO:
      _sppc_bt_put("division by zero");
      break;
    case SPPC_CHECK_REMAINDER_BY_ZERO:
      _sppc_bt_put("remainder by zero");
      break;
    case SPPC_CHECK_SIGNED_DIV:
      _sppc_bt_put("signed divide overflowed");
      break;
    default:
      _sppc_bt_put("unknown check ");
      _sppc_bt_put_dec(check);
      break;
  }
  _sppc_bt_put("\n");
  _sppc_bt_fail_end(_sppc_return_address());
}

// ==================== THREADING ====================

extern bool _unsafe_stack_wanted;

_gnu_inline
int _sppc_mutex_init_kind(pthread_mutex_t *target, const int flag) {
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, flag);
  const int err = pthread_mutex_init(target, &attr);
  pthread_mutexattr_destroy(&attr);
  return err;
}

_gnu_inline_va _gnu_nonnull(1)
void* _sppc_thread_entry(void *closure) {
  sppc_closure *const cl = (sppc_closure*)closure;
  const sppc_closure call = *cl;
  free(cl);

  // Before the callable, not after: the prologue of the s++ code about to
  // run reads this thread's unsafe stack pointer, and a thread that never
  // set one up would read a null one. There is nowhere to report a failure
  // to from here, and running on a null unsafe stack is not an option, so
  // this is one of the few places the runtime gives up outright.
  if (_unsafe_stack_wanted && sppc_unsafe_stack_up() != 0) { abort(); }
  _sppc_bt_altstack_up();

  call.fn(call.env);
  _sppc_env_free(call.env);

  // As at process exit: a task spawned on this thread and never awaited still
  // runs to completion, rather than being abandoned with the thread. The
  // runtime's memory, ring and stacks are released afterwards by its own
  // thread-exit hook.
  _gt_drain();

  // The thread is going away and its stack with it. Unlike the main
  // thread's, this one is worth handing back: a process that spawns
  // threads for its lifetime would otherwise accumulate one mapping each.
  sppc_unsafe_stack_down();
  _sppc_bt_altstack_down();
  return NULL;
}

_gnu_inline _gnu_restrict_access(write_only, 2) _gnu_nonnull(2)
_sppc_api int sppc_pthread_create(
  const sppc_closure start_routine, uint64_t *_sppc_restrict out) {
  sppc_closure *const cl = (sppc_closure*)malloc(sizeof *cl);
  if (cl == NULL) { _sppc_closure_discard(start_routine); return SPPC_ENOMEM; }
  *cl = start_routine;

  // Stored as a value, as every reader takes it: a pthread_t narrower than
  // the handle, written through it, lands in the wrong half when big-endian.
  pthread_t thread;
  _extract_err(int) pthread_create(&thread, NULL, _sppc_thread_entry, cl);
  if (err != 0) { free(cl); _sppc_closure_discard(start_routine); }
  else { *out = (uint64_t)(uintptr_t)thread; }
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_join(
  uint64_t const *_sppc_restrict handle) {
  // POSIX lets a join of the calling thread fail or hang; glibc fails it,
  // musl hangs, so it is refused here.
  if (pthread_equal((pthread_t)(uintptr_t)*handle, pthread_self())) { return SPPC_EDEADLK; }
  _extract_err(int) pthread_join((pthread_t)(uintptr_t)*handle, NULL);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_detach(
  uint64_t const *_sppc_restrict handle) {
  _extract_err(int) pthread_detach((pthread_t)(uintptr_t)*handle);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_gnu_restrict_access(read_only, 2) _gnu_restrict_access(write_only, 3)
_sppc_api void sppc_pthread_equal(
  uint64_t const *handle1, uint64_t const *handle2,
  uint64_t *_sppc_restrict out) {
  *out = pthread_equal(
    (pthread_t)(uintptr_t)*handle1, (pthread_t)(uintptr_t)*handle2) != 0;
}

_gnu_inline _gnu_restrict_access(write_only, 1)
_sppc_api void sppc_pthread_self(
  uint64_t *_sppc_restrict out) {
  *out = (uint64_t)(uintptr_t)pthread_self();
}

_gnu_inline _gnu_restrict_access(write_only, 1)
_sppc_api int sppc_pthread_mutex_init(
  uint64_t *_sppc_restrict out) {
  _pthread_handle_alloc(pthread_mutex_t)
  _extract_err(int) _sppc_mutex_init_kind(
    obj, DEBUG_BUILD ? PTHREAD_MUTEX_ERRORCHECK : PTHREAD_MUTEX_NORMAL);
  _pthread_handle_publish(out)
}

_gnu_inline _gnu_restrict_access(write_only, 1)
_sppc_api int sppc_pthread_mutex_init_recursive(
  uint64_t *_sppc_restrict out) {
  _pthread_handle_alloc(pthread_mutex_t)
  _extract_err(int) _sppc_mutex_init_kind(obj, PTHREAD_MUTEX_RECURSIVE);
  _pthread_handle_publish(out)
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_mutex_lock(
  uint64_t const *_sppc_restrict mutex) {
  _extract_err(int) _gt_mutex_lock(
    _pthread_handle(pthread_mutex_t, mutex));
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_mutex_trylock(
  uint64_t const *_sppc_restrict mutex) {
  _extract_err(int) pthread_mutex_trylock(
    _pthread_handle(pthread_mutex_t, mutex));
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_mutex_unlock(
  uint64_t const *_sppc_restrict mutex) {
  _extract_err(int) _gt_mutex_unlock(
    _pthread_handle(pthread_mutex_t, mutex));
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_mutex_destroy(
  uint64_t const *_sppc_restrict mutex) {
  pthread_mutex_t *const obj = _pthread_handle(
    pthread_mutex_t, mutex);
  _extract_err(int) pthread_mutex_destroy(obj);
  free(obj);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(write_only, 1) _gnu_nonnull(1)
_sppc_api int sppc_pthread_once_init(
  uint64_t *_sppc_restrict out) {
  _Static_assert(sizeof(_sppc_once_t) <= sizeof(uint64_t), "a once must fit in its handle");
  *out = 0; // clear the whole handle, not just the once-control inside it.
  *(_sppc_once_t*)out = (_sppc_once_t)_SPPC_ONCE_INIT;
  return 0;
}

extern _sppc_thread_local sppc_closure _sppc_once_closure;
extern _sppc_thread_local int _sppc_once_ran;

_gnu_inline_va
void _sppc_once_entry(void) {
  const sppc_closure call = _sppc_once_closure;
  _sppc_once_ran = 1;
  call.fn(call.env);
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_nonnull(1)
_sppc_api int sppc_pthread_once(
  uint64_t const *_sppc_restrict once, const sppc_closure func) {
  _sppc_once_closure = func;
  _sppc_once_ran = 0;
  _extract_err(int) _sppc_pthread_once(
    (_sppc_once_t*)once, _sppc_once_entry);
  if (_sppc_once_ran) { _sppc_env_free(func.env); }
  else { _sppc_closure_discard(func); }
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(write_only, 1)
_sppc_api int sppc_pthread_cond_init(
  uint64_t *_sppc_restrict out) {
  _pthread_handle_alloc(pthread_cond_t)
  _extract_err(int) pthread_cond_init(obj, NULL);
  _pthread_handle_publish(out)
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_gnu_restrict_access(read_only, 2)
_sppc_api int sppc_pthread_cond_wait(
  uint64_t const *_sppc_restrict cond,
  uint64_t const *_sppc_restrict mutex) {
  _extract_err(int) _gt_cond_wait_until(
    _pthread_handle(pthread_cond_t, cond),
    _pthread_handle(pthread_mutex_t, mutex),
    CLOCK_MONOTONIC, NULL);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_cond_signal(
  uint64_t const *_sppc_restrict cond) {
  _extract_err(int) pthread_cond_signal(
    _pthread_handle(pthread_cond_t, cond));
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_cond_broadcast(
  uint64_t const *_sppc_restrict cond) {
  _extract_err(int) pthread_cond_broadcast(
    _pthread_handle(pthread_cond_t, cond));
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_cond_destroy(
  uint64_t const *_sppc_restrict cond) {
  pthread_cond_t *const obj = _pthread_handle(pthread_cond_t, cond);
  _extract_err(int) pthread_cond_destroy(obj);
  free(obj);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(write_only, 1)
_sppc_api int sppc_pthread_rwlock_init(
  uint64_t *_sppc_restrict rwlock) {
  _pthread_handle_alloc(pthread_rwlock_t)
  _extract_err(int) pthread_rwlock_init(obj, NULL);
  _pthread_handle_publish(rwlock)
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_rwlock_rdlock(
  uint64_t const *_sppc_restrict rwlock) {
  _extract_err(int) _gt_rwlock_rdlock_until(
    _pthread_handle(pthread_rwlock_t, rwlock), CLOCK_MONOTONIC, NULL);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_rwlock_tryrdlock(
  uint64_t const *_sppc_restrict rwlock) {
  _extract_err(int) _sppc_pthread_rwlock_tryrdlock(
    _pthread_handle(pthread_rwlock_t, rwlock));
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_rwlock_wrlock(
  uint64_t const *_sppc_restrict rwlock) {
  _extract_err(int) _gt_rwlock_wrlock_until(
    _pthread_handle(pthread_rwlock_t, rwlock), CLOCK_MONOTONIC, NULL);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_rwlock_trywrlock(
  uint64_t const *_sppc_restrict rwlock) {
  _extract_err(int) _sppc_pthread_rwlock_trywrlock(
    _pthread_handle(pthread_rwlock_t, rwlock));
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_rwlock_unlock(
  uint64_t const *_sppc_restrict rwlock) {
  _extract_err(int) _gt_rwlock_unlock(_pthread_handle(
    pthread_rwlock_t, rwlock));
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_rwlock_destroy(
  uint64_t const *_sppc_restrict rwlock) {
  pthread_rwlock_t *const obj = _pthread_handle(
    pthread_rwlock_t, rwlock);
  _extract_err(int) pthread_rwlock_destroy(obj);
  free(obj);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_gnu_restrict_access(read_only, 3)
_sppc_api int sppc_pthread_mutex_clocklock(
  uint64_t const *_sppc_restrict mutex, const int clock,
  sppc_timespec const *_sppc_restrict duration) {
  clockid_t os_clock;
  struct timespec os_duration, deadline;
  if (_sppc_to_os_clock(clock, &os_clock) != 0 || _sppc_to_os_timespec(duration, &os_duration) != 0) { return SPPC_EINVAL; }
  _gt_deadline(os_clock, &os_duration, &deadline);
  _extract_err(int) _gt_mutex_lock_until(
    _pthread_handle(pthread_mutex_t, mutex), os_clock, &deadline);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_gnu_restrict_access(read_only, 2) _gnu_restrict_access(read_only, 4)
_sppc_api int sppc_pthread_cond_clockwait(
  uint64_t const *_sppc_restrict cond, uint64_t const *_sppc_restrict mutex,
  const int clock, sppc_timespec const *_sppc_restrict duration) {
  clockid_t os_clock;
  struct timespec os_duration, deadline;
  if (_sppc_to_os_clock(clock, &os_clock) != 0 || _sppc_to_os_timespec(duration, &os_duration) != 0) { return SPPC_EINVAL; }
  _gt_deadline(os_clock, &os_duration, &deadline);
  _extract_err(int) _gt_cond_wait_until(
    _pthread_handle(pthread_cond_t, cond),
    _pthread_handle(pthread_mutex_t, mutex),
    os_clock, &deadline);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_gnu_restrict_access(read_only, 3)
_sppc_api int sppc_pthread_rwlock_clockrdlock(
  uint64_t const *_sppc_restrict rwlock, const int clock,
  sppc_timespec const *_sppc_restrict duration) {
  clockid_t os_clock;
  struct timespec os_duration, deadline;
  if (_sppc_to_os_clock(clock, &os_clock) != 0 || _sppc_to_os_timespec(duration, &os_duration) != 0) { return SPPC_EINVAL; }
  _gt_deadline(os_clock, &os_duration, &deadline);
  _extract_err(int) _gt_rwlock_rdlock_until(
    _pthread_handle(pthread_rwlock_t, rwlock), os_clock, &deadline);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_gnu_restrict_access(read_only, 3)
_sppc_api int sppc_pthread_rwlock_clockwrlock(
  uint64_t const *_sppc_restrict rwlock, const int clock,
  sppc_timespec const *_sppc_restrict duration) {
  clockid_t os_clock;
  struct timespec os_duration, deadline;
  if (_sppc_to_os_clock(clock, &os_clock) != 0 || _sppc_to_os_timespec(duration, &os_duration) != 0) { return SPPC_EINVAL; }
  _gt_deadline(os_clock, &os_duration, &deadline);
  _extract_err(int) _gt_rwlock_wrlock_until(
    _pthread_handle(pthread_rwlock_t, rwlock), os_clock, &deadline);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(write_only, 1)
_sppc_api int sppc_pthread_spin_init(
  uint64_t *_sppc_restrict spinlock) {
  _extract_err(int) _sppc_pthread_spin_init(
    (_sppc_spinlock_t*)spinlock, 0);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_spin_lock(
  uint64_t const *_sppc_restrict spinlock) {
  _extract_err(int) _gt_spin_lock(
    (_sppc_spinlock_t*)spinlock);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_spin_trylock(
  uint64_t const *_sppc_restrict spinlock) {
  _extract_err(int) _sppc_pthread_spin_trylock(
    (_sppc_spinlock_t*)spinlock);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_spin_unlock(
  uint64_t const *_sppc_restrict spinlock) {
  _extract_err(int) _gt_spin_unlock(
    (_sppc_spinlock_t*)spinlock);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_spin_destroy(
  uint64_t const *_sppc_restrict spinlock) {
  _extract_err(int) _sppc_pthread_spin_destroy(
    (_sppc_spinlock_t*)spinlock);
  _return_normalized_pthread_err
}

// ==================== BARRIERS ====================

#if !SPPC_KERNEL_DARWIN

_gnu_inline _gnu_restrict_access(write_only, 1)
_sppc_api int sppc_pthread_barrier_init(
  uint64_t *_sppc_restrict barrier, const uint64_t count) {
  if (count == 0 || count > UINT_MAX) { return SPPC_EINVAL; }
  _pthread_handle_alloc(pthread_barrier_t)
  _extract_err(int) pthread_barrier_init(obj, NULL, count);
  _pthread_handle_publish(barrier)
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_barrier_wait(
  uint64_t const *_sppc_restrict barrier) {
  _extract_err(int) pthread_barrier_wait(_pthread_handle(pthread_barrier_t, barrier));
  if (err == PTHREAD_BARRIER_SERIAL_THREAD) { return SPPC_BARRIER_SERIAL_THREAD; }
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_barrier_destroy(
  uint64_t const *_sppc_restrict barrier) {
  pthread_barrier_t *const obj = _pthread_handle(pthread_barrier_t, barrier);
  _extract_err(int) pthread_barrier_destroy(obj);
  free(obj);
  _return_normalized_pthread_err
}

#endif

// ==================== STANDARD STREAMS ====================

// Each of fds 0, 1 and 2 read or written under a lock of its own, so that one
// call's bytes are never interleaved with another thread's (or another task's,
// as the lock is taken through the async runtime).

extern pthread_mutex_t _stdin_mutex;
extern pthread_mutex_t _stdout_mutex;
extern pthread_mutex_t _stderr_mutex;

_gnu_inline _gnu_restrict_access(write_only, 1) _gnu_restrict_access(write_only, 4) _gnu_nonnull(1, 4)
_sppc_api int sppc_stdin_read(
  char *_sppc_restrict buffer, const size_t size, const size_t count,
  ssize_t *_sppc_restrict out_n) {
  ssize_t err = 0;
  _return_if_guarded_err(
    &_stdin_mutex, sppc_read(buffer, size, count, STDIN_FILENO, &err))
  _sret_normalised_store(out_n)
  _return_normalized_err
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(write_only, 4) _gnu_nonnull(1, 4)
_sppc_api int sppc_stdout_write(
  char const *_sppc_restrict buffer, const size_t size,
  const size_t count, ssize_t *_sppc_restrict out_n) {
  ssize_t err = 0;
  _return_if_guarded_err(
    &_stdout_mutex, sppc_write(buffer, size, count, STDOUT_FILENO, &err))
  _sret_normalised_store(out_n)
  _return_normalized_err
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(write_only, 4) _gnu_nonnull(1, 4)
_sppc_api int sppc_stderr_write(
  char const *_sppc_restrict buffer, const size_t size, const size_t count,
  ssize_t *_sppc_restrict out_n) {
  ssize_t err = 0;
  _return_if_guarded_err(
    &_stderr_mutex, sppc_write(buffer, size, count, STDERR_FILENO, &err))
  _sret_normalised_store(out_n)
  _return_normalized_err
}
