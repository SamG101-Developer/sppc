#pragma once

#define _gnu_inline inline __attribute__((always_inline)) __attribute__((flatten))
#define _sppc_api __attribute__((visibility("default"))) __attribute__((nothrow))
#define _gnu_noreturn __attribute__((noreturn))
#define _gnu_restrict_access(mode, index) __attribute__((access(mode, index)))
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
#define _return_normalized_pthread_err return err != 0 ? errno : 0;
#define _return_special_error(errno_val, return_val) if (err == errno_val) { return return_val; }
#define _return_pointer return err;
#define _sret_normalised_store(into) *into = err < 0 ? -1 : err;

#define _extract_err const auto err =
#define _socket_addr_in_construction_helper socklen_t len = storage->ss_family == AF_INET ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
#define _socket_addr_out_construction_helper socklen_t len = out_storage->ss_family == AF_INET ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);

#define pthread_mutex_init_helper(flag)                                 \
    ({ pthread_mutexattr_t attr;                                        \
    pthread_mutexattr_init(&attr);                                      \
    pthread_mutexattr_settype(&attr, (flag));                           \
    const auto err_ = pthread_mutex_init((pthread_mutex_t*)out, &attr); \
    pthread_mutexattr_destroy(&attr);                                   \
    err_; })
