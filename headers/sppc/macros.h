#pragma once
#include <sppc/config.h>

// Every attribute is guarded individually: gcc and clang
// support different sets ("access" and "fd_arg" are gcc-only),
// and msvc has no "__attribute__" at all.
#if defined(__has_attribute)
  #define _sppc_has_attribute(x) __has_attribute(x)
#else
  #define _sppc_has_attribute(x) 0
#endif

// Thread-local storage. msvc spells it "__declspec(thread)";
// the C11 keyword everywhere else, falling back to the older
// gnu "__thread" before C11.
#if SPPC_CC_MSVC
  #define _sppc_thread_local __declspec(thread)
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
  #define _sppc_thread_local _Thread_local
#else
  #define _sppc_thread_local __thread
#endif

// A lock-free test-and-set flag, safe in a signal handler. C11
// atomics everywhere but msvc, whose C mode only has them
// behind an experimental switch; the interlocked intrinsic does
// the same job there.
#if SPPC_CC_MSVC
  #include <intrin.h>
  typedef volatile long _sppc_atomic_flag;
  #define _SPPC_ATOMIC_FLAG_INIT 0
  #define _sppc_atomic_flag_test_and_set(flag) (_InterlockedExchange((flag), 1) != 0)
  #define _sppc_atomic_flag_clear(flag) ((void)_InterlockedExchange((flag), 0))
#else
  #include <stdatomic.h>
  typedef atomic_flag _sppc_atomic_flag;
  #define _SPPC_ATOMIC_FLAG_INIT ATOMIC_FLAG_INIT
  #define _sppc_atomic_flag_test_and_set(flag) atomic_flag_test_and_set(flag)
  #define _sppc_atomic_flag_clear(flag) atomic_flag_clear(flag)
#endif

// The "restrict" pointer tag is C99, but msvc (and any c++
// compiler reading these headers) only knows "__restrict".
#if SPPC_CC_MSVC || defined(__cplusplus)
  #define _sppc_restrict __restrict
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
  #define _sppc_restrict restrict
#else
  #define _sppc_restrict __restrict
#endif

// The [likely/unlikely]-ness of a branch is only supported in
// gcc and clang. Fallback to "!!" for MSVC.
#if SPPC_CC_GCC || SPPC_CC_CLANG
  #define _sppc_likely(x) __builtin_expect(!!(x), 1)
  #define _sppc_unlikely(x) __builtin_expect(!!(x), 0)
#else
  #define _sppc_likely(x) (!!(x))
  #define _sppc_unlikely(x) (!!(x))
#endif

// Apply the "flatten" attribute if it present on the system. No
// alternative fallback.
#if _sppc_has_attribute(flatten)
  #define _sppc_flatten __attribute__((flatten))
#else
  #define _sppc_flatten
#endif

// Mark a function as being inlined, using different attributes
// for different compilers. Apply "flatten" too in most cases.
#if SPPC_CC_MSVC
  #define _gnu_inline __forceinline
#elif _sppc_has_attribute(always_inline)
  #define _gnu_inline inline __attribute__((always_inline)) _sppc_flatten
#else
  #define _gnu_inline inline _sppc_flatten
#endif

// For one-off slow paths split out of an always-inlined function,
// so the function's hot callers inline only the check and not the
// work behind it.
#if SPPC_CC_MSVC
  #define _gnu_noinline __declspec(noinline)
#elif _sppc_has_attribute(noinline)
  #define _gnu_noinline __attribute__((noinline))
#else
  #define _gnu_noinline
#endif

#define _gnu_inline_va inline _sppc_flatten

// Mark a function as being "no throw", using different attributes
// for different compilers.
#if SPPC_CC_MSVC
  #define _sppc_nothrow __declspec(nothrow)
#elif _sppc_has_attribute(nothrow)
  #define _sppc_nothrow __attribute__((nothrow))
#else
  #define _sppc_nothrow
#endif

// Exported from the shared library. On Windows that is
// "dllexport" whatever the compiler (mingw gcc and clang accept
// the msvc spelling); everywhere else it is default visibility,
// with the build hiding everything else. Variables take
// "_sppc_export" alone, as "nothrow" is for functions. Windows
// cannot export a thread-local variable from a dll at all,
// which matters only to the split-stack pointer.
#if SPPC_PLATFORM_WINDOWS
  #define _sppc_export __declspec(dllexport)
#elif _sppc_has_attribute(visibility)
  #define _sppc_export __attribute__((visibility("default")))
#else
  #define _sppc_export
#endif

#define _sppc_api _sppc_export _sppc_nothrow

// Mark a function as no-return - this is for abort/exit
// functions.
#if SPPC_CC_MSVC
  #define _gnu_noreturn __declspec(noreturn)
#elif _sppc_has_attribute(noreturn)
  #define _gnu_noreturn __attribute__((noreturn))
#else
  #define _gnu_noreturn
#endif

// Read/write access optimizations for pointers.
#if _sppc_has_attribute(access)
  #define _gnu_restrict_access(mode, ...) __attribute__((access(mode, __VA_ARGS__)))
#else
  #define _gnu_restrict_access(mode, ...)
#endif

// Optimize hot path functions (memory functions, io_uring
// wrappers etc).
#if _sppc_has_attribute(hot)
  #define _gnu_hot __attribute__((hot))
#else
  #define _gnu_hot
#endif

// Optimize cold path functions (stacktrace, stack management,
// io_uring boot functions etc).
#if _sppc_has_attribute(cold)
  #define _gnu_cold __attribute__((cold))
#else
  #define _gnu_cold
#endif

// Specifically used for the "sppc_aligned_alloc" function.
#if _sppc_has_attribute(alloc_align)
  #define _gnu_alloc_align(index) __attribute__((alloc_align(index)))
#else
  #define _gnu_alloc_align(index)
#endif

#if _sppc_has_attribute(alloc_size)
  #define _gnu_alloc_size(...) __attribute__((alloc_size(__VA_ARGS__)))
#else
  #define _gnu_alloc_size(...)
#endif

// Applied to the memory allocation functions, with different
// compiler behaviour.
#if SPPC_CC_MSVC
  #define _gnu_malloc __declspec(restrict)
#elif _sppc_has_attribute(malloc)
  #define _gnu_malloc __attribute__((malloc))
#else
  #define _gnu_malloc
#endif

// The family of "fd" optimizing attributes, applied to all the
// fd-stream functions.
#if _sppc_has_attribute(fd_arg)
  #define _gnu_fd_arg(index) __attribute__((fd_arg(index)))
  #define _gnu_fd_arg_read(index) __attribute__((fd_arg_read(index)))
  #define _gnu_fd_arg_write(index) __attribute__((fd_arg_write(index)))
#else
  #define _gnu_fd_arg(index)
  #define _gnu_fd_arg_read(index)
  #define _gnu_fd_arg_write(index)
#endif

// When we can guarantee that a pointer is nonnull (everything
// from s++ will be nonnull).
#if _sppc_has_attribute(nonnull)
  #define _gnu_nonnull(...) __attribute__((nonnull(__VA_ARGS__)))
#else
  #define _gnu_nonnull(...)
#endif

// Lets the compiler check the arguments of a printf-style
// function against its format string. Currently only used in
// the stacktrace management.
#if _sppc_has_attribute(format)
  #define _gnu_format_printf(fmt_index, args_index) __attribute__((format(printf, fmt_index, args_index)))
#else
  #define _gnu_format_printf(fmt_index, args_index)
#endif

// Thread-locals the library reads on every split-stack
// function entry: the initial-exec model makes each access
// a single thread-pointer-relative load rather than a call
// to "__tls_get_addr". It draws on the small static tls
// reserve a dlopen'd library gets, so it is only for small
// variables.
#if _sppc_has_attribute(tls_model)
  #define _sppc_tls_initial_exec __attribute__((tls_model("initial-exec")))
#else
  #define _sppc_tls_initial_exec
#endif

// Helper macros for maintenance.
#define _posix_syscall(rax) // SYSCALL FOR POSIX COMPLIANT SYSTEM
#define _posix_fake_syscall(reason) // SYSCALL FOR POSIX COMPLIANT SYSTEM

// Every error a public function returns is sppc's number for
// it (types.h), so the platform's goes through "_sppc_errno"
// on the way out.
#define _return_normalized_err return _sppc_unlikely(err < 0) ? _sppc_errno(errno) : 0;
#define _return_normalized_ptr_err return _sppc_unlikely(err == NULL) ? _sppc_errno(errno) : 0;

// A search that finds nothing is not a failure of the call,
// and sets no errno to report: it comes back as SPPC_ENOENT,
// with the out index left alone.
#define _return_found return _sppc_likely(err != NULL) ? 0 : SPPC_ENOENT;
#define _return_map_pointer return _sppc_unlikely(err == MAP_FAILED) ? NULL : err;
#define _return_normalized_pthread_err return _sppc_errno(err);
#define _return_if_pthread_err(call) { const int err_ = (call); if (_sppc_unlikely(err_ != 0)) { return _sppc_errno(err_); } }

// For a call that already reports sppc's numbers (a translation,
// say).
#define _return_if_sppc_err(call) { const int err_ = (call); if (_sppc_unlikely(err_ != 0)) { return err_; } }
#define _return_if_guarded_err(mutex, call)             \
  _return_if_pthread_err(_gt_mutex_lock(mutex))         \
  const int rc_ = (call);                               \
  _return_if_pthread_err(_gt_mutex_unlock(mutex))       \
  if (_sppc_unlikely(rc_ != 0)) { return rc_; }
#define _return_pointer return err;
#define _sret_normalised_store(into) *into = err < 0 ? -1 : err;
#define _sret_sign_store(into) *into = err < 0 ? -1 : err > 0 ? 1 : 0;
#define _sret_normalised_store_ptr(into) *into = (size_t)err;

// The type comes first and "const" after it, so a pointer type
// stays a const pointer: "_extract_err(void*)" is "void* const
// err", not "const void* err".
#define _extract_err(type) type const err =

// Make a blocking call through the green-thread runtime where
// it can take it, and through the plain syscall where it cannot.
#define _extract_err_async(type, attempt, fallback) \
  type _async_res = 0;                              \
  const type err = (attempt) ? _async_res : (fallback);

#define _pthread_handle(type, handle) ((type*)(uintptr_t)*(handle))

#define _pthread_handle_alloc(type)              \
  type *const obj = (type*)malloc(sizeof(type)); \
  if (_sppc_unlikely(obj == NULL)) { return SPPC_ENOMEM; }

#define _pthread_handle_publish(out)             \
  if (_sppc_unlikely(err != 0)) { free(obj); return _sppc_errno(err); } \
  *(out) = (uint64_t)(uintptr_t)obj;             \
  return 0;
