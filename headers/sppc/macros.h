#pragma once

#define _sppc_likely(x) __builtin_expect(!!(x), 1)
#define _sppc_unlikely(x) __builtin_expect(!!(x), 0)

#define _gnu_inline inline __attribute__((always_inline)) __attribute__((flatten))
#define _gnu_inline_va inline __attribute__((flatten))
#define _sppc_api __attribute__((visibility("default"))) __attribute__((nothrow))
#define _gnu_noreturn __attribute__((noreturn))
#define _gnu_restrict_access(mode, ...) __attribute__((access(mode, __VA_ARGS__)))
#define _gnu_hot __attribute__((hot))
#define _gnu_cold __attribute__((cold))
#define _gnu_alloc_align(index) __attribute__((alloc_align(index)))
#define _gnu_alloc_size(...) __attribute__((alloc_size(__VA_ARGS__)))
#define _gnu_malloc __attribute__((malloc))
#define _gnu_fd_arg(index) __attribute__((fd_arg(index)))
#define _gnu_fd_arg_read(index) __attribute__((fd_arg_read(index)))
#define _gnu_fd_arg_write(index) __attribute__((fd_arg_write(index)))
#define _gnu_nonnull(...) __attribute__((nonnull(__VA_ARGS__)))

#define _posix_syscall(rax) // SYSCALL FOR POSIX COMPLIANT SYSTEM
#define _posix_fake_syscall(reason) // SYSCALL FOR POSIX COMPLIANT SYSTEM

#define _return_normalized_err return err < 0 ? errno : 0;
#define _return_normalized_ptr_err return err == NULL ? errno : 0;
#define _return_map_pointer return err == MAP_FAILED ? NULL : err;
#define _return_normalized_pthread_err return err;
#define _return_if_pthread_err(call) { const auto err_ = (call); if (err_ != 0) { return err_; } }
#define _return_if_guarded_err(mutex, call)             \
  _return_if_pthread_err(pthread_mutex_lock(mutex))     \
  const auto rc_ = (call);                              \
  _return_if_pthread_err(pthread_mutex_unlock(mutex))   \
  if (rc_ != 0) { return rc_; }
#define _return_special_error(errno_val, return_val) if (err == errno_val) { return return_val; }
#define _return_pointer return err;
#define _sret_normalised_store(into) *into = err < 0 ? -1 : err;
#define _sret_sign_store(into) *into = err < 0 ? -1 : err > 0 ? 1 : 0;
#define _sret_normalised_store_ptr(into) *into = (size_t)err;

#define _extract_err const auto err =
#define _socket_addr_in_construction_helper socklen_t len = storage->ss_family == AF_INET ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
#define _socket_addr_out_construction_helper socklen_t len = sizeof(struct sockaddr_storage);

#define pthread_mutex_init_helper(target, flag)                       \
  ({ pthread_mutexattr_t attr;                                        \
  pthread_mutexattr_init(&attr);                                      \
  pthread_mutexattr_settype(&attr, (flag));                           \
  const auto err_ = pthread_mutex_init((target), &attr);              \
  pthread_mutexattr_destroy(&attr);                                   \
  err_; })

#define _pthread_handle(type, handle) ((type*)(uintptr_t)*(handle))

#define _pthread_handle_alloc(type)              \
  type *const obj = (type*)malloc(sizeof(type)); \
  if (obj == NULL) { return ENOMEM; }

#define _pthread_handle_publish(out)             \
  if (err != 0) { free(obj); return err; }       \
  *(out) = (uint64_t)(uintptr_t)obj;             \
  return 0;
