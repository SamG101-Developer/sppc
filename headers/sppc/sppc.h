/**
 * Safety guarantees from S++
 *   * All "out" integers will be 0 by default.
 *   * All pointers will be non-null, initialised.
 */

#pragma once
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes"

#include <sppc/macros.h>
#include <sppc/async.h>
#include <asm/ioctls.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <malloc.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/random.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/uio.h>

typedef int64_t ssize_t;
typedef int64_t off_t;

#ifndef DEBUG_BUILD // Set from cmake.
#define DEBUG_BUILD 0
#endif

extern pthread_mutex_t _stdin_mutex;
extern pthread_mutex_t _stdout_mutex;
extern pthread_mutex_t _stderr_mutex;

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(read_only, 2) _gnu_nonnull(1, 2)
char* strrstr(const char *restrict haystack, const char *restrict needle) {
  if (*needle == '\0') { return (char*)haystack + strlen(haystack); }
  auto last = (char*)NULL;
  auto p = haystack;

  while ((p = strstr(p, needle)) != NULL) {
    last = (char*)p;
    p++;
  }

  return last;
}

// ==================== BOOT CODE ====================

_gnu_inline _gnu_cold
_sppc_api int sppc_init(void) {
  signal(SIGPIPE, SIG_IGN); // let write() return EPIPE instead of killing the process when writing to a closed fd.
  signal(SIGCHLD, SIG_DFL); // ensure zombie reaping works correctly.
  signal(SIGHUP, SIG_IGN); // ignore SIGHUP to prevent accidental termination when the controlling terminal is closed.
  setlocale(LC_ALL, "en_GB.UTF-8");
  tzset();
  pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
  mallopt(M_TRIM_THRESHOLD, 128 * 1024); // trim after 128KB free
  mallopt(M_MMAP_THRESHOLD, 64 * 1024); // mmap allocations above 64KB
  gt_init(); // initialize the async green thread runtime.

  _return_if_pthread_err(pthread_mutex_init(&_stdin_mutex, NULL))
  _return_if_pthread_err(pthread_mutex_init(&_stdout_mutex, NULL))
  _return_if_pthread_err(pthread_mutex_init(&_stderr_mutex, NULL))
  return 0;
}

_gnu_inline _gnu_cold
_sppc_api int sppc_cleanup(void) {
  _return_if_pthread_err(pthread_mutex_destroy(&_stdin_mutex))
  _return_if_pthread_err(pthread_mutex_destroy(&_stdout_mutex))
  _return_if_pthread_err(pthread_mutex_destroy(&_stderr_mutex))
  return 0;
}

// ==================== THREADING ====================

_gnu_inline _gnu_restrict_access(write_only, 2)
_sppc_api int sppc_pthread_create(void (*start_routine)(void), uint64_t *restrict out) {
  _extract_err pthread_create((pthread_t*)out, NULL, (void*)start_routine, NULL);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_join(uint64_t const *restrict handle) {
  _extract_err pthread_join((pthread_t)*handle, nullptr);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_detach(uint64_t const *restrict handle) {
  _extract_err pthread_detach((pthread_t)*handle);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(read_only, 2) _gnu_restrict_access(write_only, 3)
_sppc_api void sppc_pthread_equal(uint64_t const *handle1, uint64_t const *handle2, uint64_t *restrict out) {
  *out = pthread_equal((pthread_t)*handle1, (pthread_t)*handle2) != 0;
}

_gnu_inline _gnu_restrict_access(write_only, 1)
_sppc_api void sppc_pthread_self(uint64_t *restrict out) {
  *out = pthread_self();
}

_gnu_inline _gnu_restrict_access(write_only, 1)
_sppc_api int sppc_pthread_mutex_init(uint64_t *restrict out) {
  _extract_err pthread_mutex_init_helper(DEBUG_BUILD ? PTHREAD_MUTEX_ERRORCHECK : PTHREAD_MUTEX_NORMAL);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(write_only, 1)
_sppc_api int sppc_pthread_mutex_init_recursive(uint64_t *restrict out) {
  _extract_err pthread_mutex_init_helper(PTHREAD_MUTEX_RECURSIVE);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_mutex_lock(uint64_t const *restrict mutex) {
  _extract_err pthread_mutex_lock((pthread_mutex_t*)mutex);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(read_only, 3)
_sppc_api int sppc_pthread_mutex_clocklock(uint64_t const *restrict mutex, const clockid_t clock,
  struct timespec const *restrict duration) {
  _extract_err pthread_mutex_clocklock((pthread_mutex_t*)mutex, clock, duration);
  _return_special_error(ETIMEDOUT, 1)
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_mutex_trylock(uint64_t const *restrict mutex) {
  _extract_err pthread_mutex_trylock((pthread_mutex_t*)mutex);
  _return_special_error(EBUSY, 1)
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_mutex_unlock(uint64_t const *restrict mutex) {
  _extract_err pthread_mutex_unlock((pthread_mutex_t*)mutex);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_mutex_destroy(uint64_t const *restrict mutex) {
  _extract_err pthread_mutex_destroy((pthread_mutex_t*)mutex);
  _return_normalized_pthread_err
}

_gnu_inline
_sppc_api int sppc_pthread_once(void (*func)(void)) {
  constexpr auto flag = PTHREAD_ONCE_INIT;
  _extract_err pthread_once((pthread_once_t*)&flag, func);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(write_only, 1)
_sppc_api int sppc_pthread_cond_init(uint64_t *restrict out) {
  _extract_err pthread_cond_init((pthread_cond_t*)out, nullptr);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(read_only, 2)
_sppc_api int sppc_pthread_cond_wait(uint64_t const *restrict cond, uint64_t const *restrict mutex) {
  _extract_err pthread_cond_wait((pthread_cond_t*)cond, (pthread_mutex_t*)mutex);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(read_only, 2) _gnu_restrict_access(read_only, 4)
_sppc_api int sppc_pthread_cond_clockwait(uint64_t const *restrict cond, uint64_t const *restrict mutex,
  const clockid_t clock, struct timespec const *restrict duration) {
  _extract_err pthread_cond_clockwait((pthread_cond_t*)cond, (pthread_mutex_t*)mutex, clock, duration);
  _return_special_error(ETIMEDOUT, 1)
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_cond_signal(uint64_t const *restrict cond) {
  _extract_err pthread_cond_signal((pthread_cond_t*)cond);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_cond_broadcast(uint64_t const *restrict cond) {
  _extract_err pthread_cond_broadcast((pthread_cond_t*)cond);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_cond_destroy(uint64_t const *restrict cond) {
  _extract_err pthread_cond_destroy((pthread_cond_t*)cond);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(write_only, 1)
_sppc_api int sppc_pthread_rwlock_init(uint64_t *restrict rwlock) {
  _extract_err pthread_rwlock_init((pthread_rwlock_t*)rwlock, nullptr);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_rwlock_rdlock(uint64_t const *restrict rwlock) {
  _extract_err pthread_rwlock_rdlock((pthread_rwlock_t*)rwlock);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_rwlock_tryrdlock(uint64_t const *restrict rwlock) {
  _extract_err pthread_rwlock_tryrdlock((pthread_rwlock_t*)rwlock);
  _return_special_error(EBUSY, 1)
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(read_only, 3)
_sppc_api int sppc_pthread_rwlock_clockrdlock(uint64_t const *restrict rwlock, const clockid_t clock,
  struct timespec const *restrict duration) {
  _extract_err pthread_rwlock_clockrdlock((pthread_rwlock_t*)rwlock, clock, duration);
  _return_special_error(ETIMEDOUT, 1)
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_rwlock_wrlock(uint64_t const *restrict rwlock) {
  _extract_err pthread_rwlock_wrlock((pthread_rwlock_t*)rwlock);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_rwlock_trywrlock(uint64_t const *restrict rwlock) {
  _extract_err pthread_rwlock_trywrlock((pthread_rwlock_t*)rwlock);
  _return_special_error(EBUSY, 1)
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(read_only, 3)
_sppc_api int sppc_pthread_rwlock_clockwrlock(uint64_t const *restrict rwlock, const clockid_t clock,
  struct timespec const *restrict duration) {
  _extract_err pthread_rwlock_clockwrlock((pthread_rwlock_t*)rwlock, clock, duration);
  _return_special_error(ETIMEDOUT, 1)
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_rwlock_unlock(uint64_t const *restrict rwlock) {
  _extract_err pthread_rwlock_unlock((pthread_rwlock_t*)rwlock);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_rwlock_destroy(uint64_t const *restrict rwlock) {
  _extract_err pthread_rwlock_destroy((pthread_rwlock_t*)rwlock);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(write_only, 1)
_sppc_api int sppc_pthread_barrier_init(uint64_t *restrict barrier, const uint64_t count) {
  _extract_err pthread_barrier_init((pthread_barrier_t*)barrier, nullptr, count);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_barrier_wait(uint64_t const *restrict barrier) {
  _extract_err pthread_barrier_wait((pthread_barrier_t*)barrier);
  _return_special_error(PTHREAD_BARRIER_SERIAL_THREAD, 1)
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_barrier_destroy(uint64_t const *restrict barrier) {
  _extract_err pthread_barrier_destroy((pthread_barrier_t*)barrier);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(write_only, 1)
_sppc_api int sppc_pthread_spin_init(uint64_t *restrict spinlock) {
  _extract_err pthread_spin_init((pthread_spinlock_t*)spinlock, 0);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_spin_lock(uint64_t const *restrict spinlock) {
  _extract_err pthread_spin_lock((pthread_spinlock_t*)spinlock);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_spin_trylock(uint64_t const *restrict spinlock) {
  _extract_err pthread_spin_trylock((pthread_spinlock_t*)spinlock);
  _return_special_error(EBUSY, 1)
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_spin_unlock(uint64_t const *restrict spinlock) {
  _extract_err pthread_spin_unlock((pthread_spinlock_t*)spinlock);
  _return_normalized_pthread_err
}

_gnu_inline _gnu_restrict_access(read_only, 1)
_sppc_api int sppc_pthread_spin_destroy(uint64_t const *restrict spinlock) {
  _extract_err pthread_spin_destroy((pthread_spinlock_t*)spinlock);
  _return_normalized_pthread_err
}

// ==================== HEAP ALLOCATION ====================

_gnu_inline _gnu_hot _gnu_malloc _gnu_alloc_size(1)
_sppc_api void* sppc_malloc(const size_t size) {
  _extract_err malloc(size);
  _return_pointer
}

_gnu_inline _gnu_malloc _gnu_alloc_size(1) _gnu_alloc_align(2)
_sppc_api void* sppc_aligned_alloc(const size_t size, const size_t alignment) {
  _extract_err aligned_alloc(size, alignment);
  _return_pointer
}

_gnu_inline _gnu_hot _gnu_malloc _gnu_alloc_size(1, 2)
_sppc_api void* sppc_calloc(const size_t num, const size_t size) {
  _extract_err calloc(num, size);
  _return_pointer
}

_gnu_inline _gnu_hot _gnu_alloc_size(2)
_sppc_api void* sppc_realloc(void *ptr, const size_t new_size) {
  _extract_err realloc(ptr, new_size);
  _return_pointer
}

_gnu_inline _gnu_hot
_sppc_api void sppc_free(void *ptr) {
  free(ptr);
}

_gnu_inline _gnu_hot _gnu_restrict_access(write_only, 1) _gnu_restrict_access(read_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_memcpy(void *restrict dest, void const *restrict src, const size_t size, const size_t dest_index,
  const size_t src_index) {
  _extract_err memcpy((char*)dest + dest_index, (char const*)src + src_index, size);
  _return_normalized_err
}

_gnu_inline _gnu_hot _gnu_restrict_access(write_only, 1) _gnu_restrict_access(read_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_memmove(void *restrict dest, void const *restrict src, const size_t size) {
  _extract_err memmove(dest, src, size);
  _return_normalized_err
}

_gnu_inline _gnu_hot _gnu_restrict_access(write_only, 1) _gnu_nonnull(1)
_sppc_api int sppc_memset(void *restrict dest, const int value, const size_t size, const size_t dest_index) {
  _extract_err memset((char*)dest + dest_index, value, size);
  _return_normalized_err
}

_gnu_inline _gnu_hot _gnu_restrict_access(read_only, 1) _gnu_restrict_access(read_only, 2)
_gnu_restrict_access(write_only, 4) _gnu_nonnull(1, 2, 4)
_sppc_api int sppc_memcmp(void const *ptr1, void const *ptr2, const size_t size, int *restrict out) {
  _extract_err memcmp(ptr1, ptr2, size);
  _sret_normalised_store(out)
  _return_normalized_err
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(read_only, 3) _gnu_restrict_access(write_only, 5)
_gnu_nonnull(1, 3, 5)
_sppc_api int sppc_memmem(void const *haystack, const size_t haystack_size, void const *needle,
  const size_t needle_size, size_t *restrict out) {
  _extract_err memmem(haystack, haystack_size, needle, needle_size);
  if (err != NULL) { *out = (size_t)((char*)err - (char*)haystack); }
  _return_normalized_ptr_err
}

// ==================== SYSCALLS ====================

_posix_syscall(0)
_gnu_inline _gnu_fd_arg_read(4) _gnu_restrict_access(write_only, 1) _gnu_restrict_access(write_only, 5)
_gnu_nonnull(1, 5)
_sppc_api int sppc_read(char *restrict buffer, const size_t size, const size_t count, const int fd,
  ssize_t *restrict out_n) {
  _extract_err read(fd, buffer, size * count);
  _sret_normalised_store(out_n)
  _return_normalized_err
}

_posix_syscall(1)
_gnu_inline _gnu_fd_arg_write(4) _gnu_restrict_access(read_only, 1) _gnu_restrict_access(write_only, 5)
_gnu_nonnull(1, 5)
_sppc_api int sppc_write(char const *restrict buffer, const size_t size, const size_t count, const int fd,
  ssize_t *restrict out_n) {
  _extract_err write(fd, buffer, size * count);
  _sret_normalised_store(out_n)
  _return_normalized_err
}

_posix_syscall(2)
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(write_only, 4) _gnu_nonnull(1, 4)
_sppc_api int sppc_open(char const *restrict path, const int flags, const mode_t mode, int *restrict out_fd) {
  _extract_err open(path, flags, mode);
  _sret_normalised_store(out_fd)
  _return_normalized_err
}

_posix_syscall(3)
_gnu_inline _gnu_fd_arg(1)
_sppc_api int sppc_close(const int fd) {
  _extract_err close(fd);
  _return_normalized_err
}

_posix_syscall(4)
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(write_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_stat(char const *restrict path, struct stat *restrict out) {
  _extract_err stat(path, out);
  _return_normalized_err
}

_posix_syscall(5)
_gnu_inline _gnu_fd_arg(1) _gnu_restrict_access(write_only, 2) _gnu_nonnull(2)
_sppc_api int sppc_fstat(const int fd, struct stat *restrict out) {
  _extract_err fstat(fd, out);
  _return_normalized_err
}

_posix_syscall(6)
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(write_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_lstat(char const *restrict path, struct stat *restrict out) {
  _extract_err lstat(path, out);
  _return_normalized_err
}

_posix_syscall(7)
_gnu_inline _gnu_restrict_access(read_write, 1) _gnu_restrict_access(write_only, 4) _gnu_nonnull(1, 4)
_sppc_api int sppc_poll(struct pollfd *restrict fds, const nfds_t count, const int timeout, int *restrict out_n) {
  _extract_err poll(fds, count, timeout);
  _sret_normalised_store(out_n)
  _return_normalized_err
}

_posix_syscall(8)
_gnu_inline _gnu_fd_arg(1) _gnu_restrict_access(write_only, 4) _gnu_nonnull(4)
_sppc_api int sppc_lseek(const int fd, const off_t offset, const int whence, off_t *restrict out_pos) {
  _extract_err lseek(fd, offset, whence);
  _sret_normalised_store(out_pos)
  _return_normalized_err
}

_posix_syscall(9)
_gnu_inline _gnu_malloc _gnu_alloc_size(2) _gnu_fd_arg(1)
_sppc_api void* sppc_mmap(const int fd, const size_t length, const int prot, const int flags, const off_t offset) {
  // NULL as the hint lets the kernel place the mapping. Returns the mapping
  // like the other allocators, NULL on failure with errno set.
  _extract_err mmap(NULL, length, prot, flags, fd, offset);
  _return_map_pointer
}

_posix_syscall(10)
_gnu_inline _gnu_nonnull(1)
_sppc_api int sppc_memprotect(void *addr, const size_t size, const int prot) {
  _extract_err mprotect(addr, size, prot);
  _return_normalized_err
}

_posix_syscall(11)
_gnu_inline _gnu_restrict_access(read_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_munmap(void *restrict addr, size_t const *restrict length) {
  _extract_err munmap(addr, *length);
  _return_normalized_err
}

_posix_syscall(17)
_gnu_inline _gnu_fd_arg_read(1) _gnu_restrict_access(write_only, 2) _gnu_restrict_access(write_only, 6)
_gnu_nonnull(2, 6)
_sppc_api int sppc_pread(const int fd, void *restrict buffer, const size_t size, const size_t count, const off_t offset,
  ssize_t *restrict out_n) {
  _extract_err pread(fd, buffer, size * count, offset);
  _sret_normalised_store(out_n)
  _return_normalized_err
}

_posix_syscall(18)
_gnu_inline _gnu_fd_arg_write(1) _gnu_restrict_access(read_only, 2) _gnu_restrict_access(write_only, 6)
_gnu_nonnull(2, 6)
_sppc_api int sppc_pwrite(const int fd, void const *restrict buffer, const size_t size, const size_t count,
  const off_t offset, ssize_t *restrict out_n) {
  _extract_err pwrite(fd, buffer, size * count, offset);
  _sret_normalised_store(out_n)
  _return_normalized_err
}

_posix_syscall(19)
_gnu_inline _gnu_fd_arg_read(1) _gnu_restrict_access(read_only, 2) _gnu_restrict_access(write_only, 4)
_gnu_nonnull(2, 4)
_sppc_api int sppc_readv(const int fd, struct iovec const *restrict iov, const int iov_count, ssize_t *restrict out_n) {
  _extract_err readv(fd, iov, iov_count);
  _sret_normalised_store(out_n)
  _return_normalized_err
}

_posix_syscall(20)
_gnu_inline _gnu_fd_arg_write(1) _gnu_restrict_access(read_only, 2) _gnu_restrict_access(write_only, 4)
_gnu_nonnull(2, 4)
_sppc_api int sppc_writev(const int fd, struct iovec const *restrict iov, const int iov_count,
  ssize_t *restrict out_n) {
  _extract_err writev(fd, iov, iov_count);
  _sret_normalised_store(out_n)
  _return_normalized_err
}

_posix_syscall(21)
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(write_only, 3) _gnu_nonnull(1, 3)
_sppc_api int sppc_access(char const *restrict path, const int flags, int *restrict out) {
  _extract_err access(path, flags);
  _sret_normalised_store(out)
  _return_normalized_err
}

_posix_syscall(22)
_gnu_inline _gnu_restrict_access(write_only, 1) _gnu_restrict_access(write_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_pipe(int *restrict out_read_fd, int *restrict out_write_fd) {
  int fds[2];
  _extract_err pipe2(fds, O_CLOEXEC);
  if (err == 0) {
    *out_read_fd = fds[0];
    *out_write_fd = fds[1];
  }
  _return_normalized_err
}

_posix_syscall(23)
_gnu_inline _gnu_restrict_access(read_only, 2) _gnu_restrict_access(read_only, 3) _gnu_restrict_access(read_only, 4)
_gnu_restrict_access(write_only, 5) _gnu_nonnull(2, 3, 4, 5)
_sppc_api int sppc_select(const int nfds, fd_set *restrict readfds, fd_set *restrict writefds,
  fd_set *restrict exceptfds, struct timeval *restrict timeout, int *restrict out_n) {
  _extract_err select(nfds, readfds, writefds, exceptfds, timeout);
  _sret_normalised_store(out_n)
  _return_normalized_err
}

_posix_syscall(24)
_gnu_inline
_sppc_api void sppc_sched_yield(void) {
  sched_yield();
}

_posix_syscall(26)
_gnu_inline _gnu_restrict_access(read_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_msync(void *restrict addr, size_t const *restrict length, const int flags) {
  _extract_err msync(addr, *length, flags);
  _return_normalized_err
}

_posix_syscall(28)
_gnu_inline _gnu_restrict_access(read_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_madvise(void *restrict addr, size_t const *restrict length, const int advice) {
  _extract_err madvise(addr, *length, advice);
  _return_normalized_err
}

_posix_syscall(33)
_gnu_inline _gnu_fd_arg(1) _gnu_restrict_access(write_only, 2) _gnu_nonnull(2)
_sppc_api int sppc_dup(const int fd, int *restrict out_fd) {
  _extract_err dup(fd);
  _sret_normalised_store(out_fd)
  _return_normalized_err
}

_posix_syscall(34)
_gnu_inline _gnu_fd_arg(1)
_sppc_api int sppc_dup2(const int fd, const int target_fd) {
  _extract_err dup2(fd, target_fd);
  _return_normalized_err
}

_posix_syscall(39)
_gnu_inline _gnu_restrict_access(write_only, 1) _gnu_nonnull(1)
_sppc_api void sppc_get_pid(pid_t *restrict out_pid) {
  *out_pid = getpid();
}

_posix_syscall(40)
_gnu_inline
_sppc_api int sppc_sendfile(const int from_fd, const int to_fd, const off_t *offset, const size_t count) {
  _extract_err sendfile(to_fd, from_fd, (off_t*)offset, count);
  _return_normalized_err
}

_posix_syscall(41)
_gnu_inline _gnu_restrict_access(write_only, 4) _gnu_nonnull(4)
_sppc_api int sppc_socket(const int domain, const int type, const int protocol, int *restrict out_fd) {
  _extract_err socket(domain, type, protocol);
  _sret_normalised_store(out_fd)
  _return_normalized_err
}

_posix_syscall(42)
_gnu_inline _gnu_fd_arg(1) _gnu_restrict_access(read_only, 2) _gnu_nonnull(2)
_sppc_api int sppc_connect(const int fd, struct sockaddr_storage const *restrict storage) {
  _socket_addr_in_construction_helper
  _extract_err connect(fd, (struct sockaddr*)storage, len);
  _return_normalized_err
}

_posix_syscall(43)
_gnu_inline _gnu_fd_arg(1) _gnu_restrict_access(write_only, 2) _gnu_restrict_access(write_only, 3) _gnu_nonnull(2, 3)
_sppc_api int sppc_accept(const int fd, struct sockaddr_storage *restrict out_storage, int *restrict out_fd) {
  _socket_addr_out_construction_helper
  _extract_err accept4(fd, (struct sockaddr*)out_storage, &len, O_CLOEXEC);
  _sret_normalised_store(out_fd)
  _return_normalized_err
}

_posix_syscall(44)
_gnu_inline _gnu_fd_arg_write(1) _gnu_restrict_access(read_only, 2) _gnu_restrict_access(read_only, 4)
_gnu_restrict_access(write_only, 5) _gnu_nonnull(2, 4, 5)
_sppc_api int sppc_sendto(const int fd, char const *data, const size_t size,
  struct sockaddr_storage const *restrict storage, ssize_t *restrict out_n) {
  _socket_addr_in_construction_helper
  _extract_err sendto(fd, data, size, 0, (struct sockaddr*)storage, len);
  _sret_normalised_store(out_n)
  _return_normalized_err
}

_posix_syscall(45)
_gnu_inline _gnu_fd_arg_read(1) _gnu_restrict_access(write_only, 2) _gnu_restrict_access(write_only, 4)
_gnu_restrict_access(write_only, 5) _gnu_nonnull(2, 4, 5)
_sppc_api int sppc_recvfrom(const int fd, char *buffer, const size_t size,
  struct sockaddr_storage *restrict out_storage, ssize_t *restrict out_n) {
  _socket_addr_out_construction_helper
  _extract_err recvfrom(fd, buffer, size, 0, (struct sockaddr*)out_storage, &len);
  _sret_normalised_store(out_n)
  _return_normalized_err
}

_posix_fake_syscall("Wrapper around `sendto`, using a nullptr storage (not expressible in S++)")
_gnu_inline _gnu_fd_arg_write(1) _gnu_restrict_access(read_only, 2) _gnu_restrict_access(write_only, 5)
_gnu_nonnull(2, 5)
_sppc_api int sppc_send(const int fd, char const *data, const size_t size, const int flags, ssize_t *restrict out_n) {
  _extract_err send(fd, data, size, flags);
  _sret_normalised_store(out_n)
  _return_normalized_err
}

_posix_fake_syscall("Wrapper around `recvfrom`, using a nullptr storage (not expressible in S++)")
_gnu_inline _gnu_fd_arg_read(1) _gnu_restrict_access(write_only, 2) _gnu_restrict_access(write_only, 5)
_gnu_nonnull(2, 5)
_sppc_api int sppc_recv(const int fd, char *buffer, const size_t size, const int flags, ssize_t *restrict out_n) {
  _extract_err recv(fd, buffer, size, flags);
  _sret_normalised_store(out_n)
  _return_normalized_err
}

_posix_syscall(48)
_gnu_inline _gnu_fd_arg(1)
_sppc_api int sppc_shutdown(const int fd, const int how) {
  _extract_err shutdown(fd, how);
  _return_normalized_err
}

_posix_syscall(49)
_gnu_inline _gnu_fd_arg(1) _gnu_restrict_access(read_only, 2) _gnu_nonnull(2)
_sppc_api int sppc_bind(const int fd, struct sockaddr_storage const *restrict storage) {
  _socket_addr_in_construction_helper
  _extract_err bind(fd, (struct sockaddr*)storage, len);
  _return_normalized_err
}

_posix_syscall(50)
_gnu_inline _gnu_fd_arg(1)
_sppc_api int sppc_listen(const int fd, const int backlog) {
  _extract_err listen(fd, backlog);
  _return_normalized_err
}

_posix_syscall(51)
_gnu_inline _gnu_fd_arg(1) _gnu_restrict_access(write_only, 2) _gnu_nonnull(2)
_sppc_api int sppc_getsockname(const int fd, struct sockaddr_storage *restrict out_storage) {
  _socket_addr_out_construction_helper
  _extract_err getsockname(fd, (struct sockaddr*)out_storage, &len);
  _return_normalized_err
}

_gnu_inline _gnu_fd_arg(1) _gnu_restrict_access(write_only, 2) _gnu_nonnull(2)
_sppc_api int sppc_getpeername(const int fd, struct sockaddr_storage *restrict out_storage) {
  _socket_addr_out_construction_helper
  _extract_err getpeername(fd, (struct sockaddr*)out_storage, &len);
  _return_normalized_err
}

_posix_syscall(60)
_gnu_inline _gnu_noreturn _gnu_cold
_sppc_api void sppc_exit(const int status) {
  sppc_cleanup();
  exit(status);
}

_posix_syscall(62)
_gnu_inline
_sppc_api int sppc_signal(const pid_t pid, const int signal) {
  _extract_err kill(pid, signal);
  _return_normalized_err
}

_posix_syscall(72)
_gnu_inline_va _gnu_fd_arg(1)
_sppc_api int sppc_fcntl(const int fd, const int cmd, ...) {
  va_list ap;
  va_start(ap, cmd);
  _extract_err fcntl(fd, cmd, va_arg(ap, void *));
  va_end(ap);
  _return_normalized_err
}

_posix_syscall(74)
_gnu_inline _gnu_fd_arg(1)
_sppc_api int sppc_fsync(const int fd) {
  _extract_err fsync(fd);
  _return_normalized_err
}

_posix_syscall(75)
_gnu_inline _gnu_fd_arg(1)
_sppc_api int sppc_fdatasync(const int fd) {
  _extract_err fdatasync(fd);
  _return_normalized_err
}

_posix_syscall(76)
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_nonnull(1)
_sppc_api int sppc_truncate(char const *restrict path, const off_t length) {
  _extract_err truncate(path, length);
  _return_normalized_err
}

_posix_syscall(77)
_gnu_inline _gnu_fd_arg_write(1)
_sppc_api int sppc_ftruncate(const int fd, const off_t length) {
  _extract_err ftruncate(fd, length);
  _return_normalized_err
}

_posix_syscall(79)
_gnu_inline _gnu_restrict_access(write_only, 1) _gnu_nonnull(1)
_sppc_api int sppc_getcwd(char *restrict buffer, const size_t size) {
  _extract_err getcwd(buffer, size);
  _return_normalized_err
}

_posix_syscall(80)
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_nonnull(1)
_sppc_api int sppc_chdir(char const *restrict path) {
  _extract_err chdir(path);
  _return_normalized_err
}

_posix_syscall(82)
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(read_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_rename(char const *restrict old_path, char const *restrict new_path) {
  _extract_err rename(old_path, new_path);
  _return_normalized_err
}

_posix_syscall(83)
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_nonnull(1)
_sppc_api int sppc_mkdir(char const *restrict path, const mode_t mode) {
  _extract_err mkdir(path, mode);
  _return_normalized_err
}

_posix_syscall(84)
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_nonnull(1)
_sppc_api int sppc_rmdir(char const *restrict path) {
  _extract_err rmdir(path);
  _return_normalized_err
}

_posix_syscall(86)
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(read_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_link(char const *restrict target, char const *restrict linkpath) {
  _extract_err link(target, linkpath);
  _return_normalized_err
}

_posix_syscall(87)
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(read_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_symlink(char const *restrict target, char const *restrict linkpath) {
  _extract_err symlink(target, linkpath);
  _return_normalized_err
}

_posix_syscall(88)
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_nonnull(1)
_sppc_api int sppc_unlink(char const *restrict path) {
  _extract_err unlink(path);
  _return_normalized_err
}

_posix_syscall(89)
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(write_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_readlink(char const *restrict path, char *restrict buffer, const size_t buffer_size) {
  _extract_err readlink(path, buffer, buffer_size);
  _return_normalized_err
}

_posix_syscall(90)
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_nonnull(1)
_sppc_api int sppc_chmod(char const *restrict path, const mode_t mode) {
  _extract_err chmod(path, mode);
  _return_normalized_err
}

_posix_syscall(92)
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_nonnull(1)
_sppc_api int sppc_chown(char const *restrict path, const uid_t owner, const gid_t group) {
  _extract_err chown(path, owner, group);
  _return_normalized_err
}

_posix_syscall(94)
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_nonnull(1)
_sppc_api int sppc_lchown(char const *restrict path, const uid_t owner, const gid_t group) {
  _extract_err lchown(path, owner, group);
  _return_normalized_err
}

_posix_syscall(110)
_gnu_inline _gnu_restrict_access(write_only, 1) _gnu_nonnull(1)
_sppc_api void sppc_get_ppid(pid_t *restrict out_ppid) {
  *out_ppid = getppid();
}

_posix_syscall(149)
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_nonnull(1)
_sppc_api int sppc_mlock(const void *addr, const size_t size) {
  _extract_err mlock(addr, size);
  _return_normalized_err
}

_posix_syscall(150)
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_nonnull(1)
_sppc_api int sppc_munlock(const void *addr, const size_t size) {
  _extract_err munlock(addr, size);
  _return_normalized_err
}

_posix_syscall(228)
_gnu_inline _gnu_restrict_access(write_only, 2) _gnu_nonnull(2)
_sppc_api int sppc_clock_gettime(const clockid_t clock_id, struct timespec *restrict out_tp) {
  _extract_err clock_gettime(clock_id, out_tp);
  _return_normalized_err
}

_posix_syscall(230)
_gnu_inline _gnu_restrict_access(read_only, 3) _gnu_nonnull(3)
_sppc_api int sppc_clock_nanosleep(const clockid_t clock, const int flags, struct timespec const *restrict duration) {
  _extract_err clock_nanosleep(clock, flags, duration, NULL);
  _return_normalized_err
}

_posix_syscall(280)
_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_nonnull(1)
_sppc_api int sppc_utimensat(char const *restrict path, const int flags) {
  _extract_err utimensat(AT_FDCWD, path, NULL, flags);
  _return_normalized_err
}

_posix_syscall(318)
_gnu_inline _gnu_restrict_access(write_only, 2) _gnu_nonnull(2)
_sppc_api int sppc_getrandom(const size_t size, char *restrict out) {
  _extract_err getrandom(out, size, 0);
  _return_normalized_err
}

// ==================== STDLIB ====================

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(write_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_realpath(char const *restrict path, char *restrict buffer) {
  _extract_err realpath(path, buffer);
  _return_normalized_err
}

_gnu_inline _gnu_restrict_access(read_write, 1) _gnu_restrict_access(write_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_mktemp(char *restrict path, int *restrict out_fd) {
  _extract_err mkostemp(path, O_CLOEXEC);
  *out_fd = err < 0 ? -1 : err;
  _return_normalized_err
}

_gnu_inline _gnu_restrict_access(read_write, 1) _gnu_nonnull(1)
_sppc_api int sppc_mktemp_dir(char *restrict path) {
  _extract_err mkdtemp(path);
  _return_normalized_err
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(write_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_statvfs(char const *restrict path, struct statvfs *restrict out) {
  _extract_err statvfs(path, out);
  _return_normalized_err
}

_gnu_inline _gnu_fd_arg_read(1) _gnu_fd_arg_write(2)
_sppc_api int sppc_copyfile(const int fd_in, const int fd_out, const size_t len, const int flags) {
  _extract_err copy_file_range(fd_in, NULL, fd_out, NULL, len, flags);
  _return_normalized_err
}

_gnu_inline _gnu_restrict_access(write_only, 1) _gnu_restrict_access(read_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_strcpy(char *restrict dest, char const *restrict src) {
  _extract_err strcpy(dest, src);
  _return_normalized_ptr_err
}

_gnu_inline _gnu_restrict_access(read_write, 1) _gnu_restrict_access(read_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_strcat(char *restrict dest, char const *restrict src) {
  _extract_err strcat(dest, src);
  _return_normalized_err
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(read_only, 2) _gnu_restrict_access(write_only, 3)
_gnu_nonnull(1, 2, 3)
_sppc_api int sppc_strcmp(char const *str1, char const *str2, bool *restrict out) {
  _extract_err strcmp(str1, str2);
  *out = err == 0 ? true : false;
  _return_normalized_err
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(read_only, 2) _gnu_restrict_access(write_only, 3)
_gnu_nonnull(1, 2, 3)
_sppc_api int sppc_strcasecmp(char const *str1, char const *str2, bool *restrict out) {
  _extract_err strcasecmp(str1, str2);
  *out = err == 0 ? true : false;
  _return_normalized_err
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(write_only, 3) _gnu_nonnull(1, 3)
_sppc_api int sppc_strchr(char const *str, const char ch, size_t *restrict out_idx) {
  _extract_err strchr(str, ch);
  if (err != NULL) { *out_idx = (int)(err - str); }
  _return_normalized_ptr_err
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(write_only, 3) _gnu_nonnull(1, 3)
_sppc_api int sppc_strrchr(char const *str, const char ch, size_t *restrict out_idx) {
  _extract_err strrchr(str, ch);
  if (err != NULL) { *out_idx = (int)(err - str); }
  _return_normalized_ptr_err
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(read_only, 2) _gnu_restrict_access(write_only, 3)
_gnu_nonnull(1, 2, 3)
_sppc_api int sppc_strstr(char const *haystack, char const *needle, size_t *restrict out_idx) {
  _extract_err strstr(haystack, needle);
  if (err != NULL) { *out_idx = (int)(err - haystack); }
  _return_normalized_ptr_err
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(read_only, 2) _gnu_restrict_access(write_only, 3)
_gnu_nonnull(1, 2, 3)
_sppc_api int sppc_strrstr(char const *haystack, char const *needle, size_t *restrict out_idx) {
  _extract_err strrstr(haystack, needle);
  if (err != NULL) { *out_idx = (int)(err - haystack); }
  _return_normalized_ptr_err
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(read_only, 2) _gnu_restrict_access(write_only, 3)
_gnu_nonnull(1, 2, 3)
_sppc_api int sppc_strcasestr(char const *haystack, char const *needle, size_t *restrict out_idx) {
  _extract_err strcasestr(haystack, needle);
  if (err != NULL) { *out_idx = (int)(err - haystack); }
  _return_normalized_ptr_err
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(read_only, 2) _gnu_restrict_access(write_only, 3)
_gnu_nonnull(1, 2, 3)
_sppc_api int sppc_strpbrk(char const *string, char const *accept, size_t *restrict out_idx) {
  _extract_err strpbrk(string, accept);
  if (err != NULL) { *out_idx = (int)(err - string); }
  _return_normalized_ptr_err
}

_gnu_inline _gnu_malloc _gnu_restrict_access(read_only, 1) _gnu_nonnull(1)
_sppc_api void* sppc_strdup(char const *str) {
  _extract_err strdup(str);
  _return_pointer
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(write_only, 2, 3) _gnu_nonnull(1, 2)
_sppc_api int sppc_getenv(char const *restrict key, char *restrict out, const size_t size) {
  // Copies the whole value, not just its first byte, and needs `size` to do
  // that safely. Truncation is reported rather than returned silently.
  if (size == 0) { return ERANGE; }
  _extract_err secure_getenv(key);
  if (err == NULL) { *out = '\0'; return ENOENT; }

  const auto len = strlen(err);
  if (len >= size) { *out = '\0'; return ERANGE; }
  memcpy(out, err, len + 1);
  return 0;
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(read_only, 2) _gnu_nonnull(1, 2)
_sppc_api int sppc_setenv(char const *restrict key, char const *restrict val, const bool overwrite) {
  _extract_err setenv(key, val, overwrite ? 1 : 0);
  _return_normalized_err
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_nonnull(1)
_sppc_api int sppc_unsetenv(char const *restrict key) {
  _extract_err unsetenv(key);
  _return_normalized_err
}

_gnu_inline _gnu_noreturn _gnu_cold
_sppc_api void sppc_halt(const int status) {
  _exit(status);
}

_gnu_inline _gnu_noreturn _gnu_cold
_sppc_api void sppc_abort() {
  abort();
}

// ==================== SAFE SOCKETS ====================

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(write_only, 3) _gnu_nonnull(1, 3)
_sppc_api int sppc_set_sockaddr_v4(uint8_t const *octets, const uint16_t port,
  struct sockaddr_storage *restrict out_storage) {
  const auto addr = (struct sockaddr_in*)out_storage;
  addr->sin_family = AF_INET;
  addr->sin_port = htons(port);
  memset(&addr->sin_zero, 0, sizeof(addr->sin_zero));
  memcpy(&addr->sin_addr.s_addr, octets, 4);
  return 0;
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(write_only, 3) _gnu_nonnull(1, 3)
_sppc_api int sppc_set_sockaddr_v6(uint16_t const *segments, const uint16_t port,
  struct sockaddr_storage *restrict out_storage) {
  const auto addr = (struct sockaddr_in6*)out_storage;
  addr->sin6_family = AF_INET6;
  addr->sin6_port = htons(port);
  addr->sin6_flowinfo = 0;
  addr->sin6_scope_id = 0;
  memcpy(&addr->sin6_addr.s6_addr, segments, 16);
  return 0;
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(write_only, 2) _gnu_restrict_access(write_only, 3)
_gnu_nonnull(1, 2, 3)
_sppc_api int sppc_get_sockaddr_v4(struct sockaddr_storage const *restrict storage, uint8_t *out_octets,
  uint16_t *out_port) {
  const auto addr = (struct sockaddr_in*)storage;
  memcpy(out_octets, &addr->sin_addr.s_addr, 4);
  *out_port = ntohs(addr->sin_port);
  return 0;
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(write_only, 2) _gnu_restrict_access(write_only, 3)
_gnu_nonnull(1, 2, 3)
_sppc_api int sppc_get_sockaddr_v6(struct sockaddr_storage const *restrict storage, uint16_t *out_segments,
  uint16_t *out_port) {
  const auto addr = (struct sockaddr_in6*)storage;
  memcpy(out_segments, &addr->sin6_addr.s6_addr, 16);
  *out_port = ntohs(addr->sin6_port);
  return 0;
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(write_only, 2) _gnu_nonnull(1, 2)
_sppc_api void sppc_sockaddr_family(struct sockaddr_storage const *restrict storage, int *restrict out_family) {
  *out_family = storage->ss_family;
}

_gnu_inline _gnu_fd_arg(1) _gnu_restrict_access(read_only, 4) _gnu_nonnull(4)
_sppc_api int sppc_setsockopt(const int fd, const int level, const int optname, int const *restrict optval) {
  constexpr auto optlen = (socklen_t)sizeof(optval);
  _extract_err setsockopt(fd, level, optname, optval, optlen);
  _return_normalized_err
}

_gnu_inline _gnu_fd_arg(1) _gnu_restrict_access(write_only, 4) _gnu_nonnull(4)
_sppc_api int sppc_getsockopt(const int fd, const int level, const int optname, int *restrict optval) {
  auto optlen = (socklen_t)sizeof(optval);
  _extract_err getsockopt(fd, level, optname, optval, &optlen);
  _return_normalized_err
}

// ==================== SPECIALISED SYSCALLS ====================

_gnu_inline _gnu_restrict_access(write_only, 1) _gnu_restrict_access(write_only, 4) _gnu_nonnull(1, 4)
_sppc_api int sppc_stdin_read(char *restrict buffer, const size_t size, const size_t count, ssize_t *restrict out_n) {
  ssize_t err = 0;
  _return_if_guarded_err(&_stdin_mutex, sppc_read(buffer, size, count, STDIN_FILENO, &err))
  _sret_normalised_store(out_n)
  _return_normalized_err
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(write_only, 4) _gnu_nonnull(1, 4)
_sppc_api int sppc_stdout_write(char const *restrict buffer, const size_t size, const size_t count,
  ssize_t *restrict out_n) {
  ssize_t err = 0;
  _return_if_guarded_err(&_stdout_mutex, sppc_write(buffer, size, count, STDOUT_FILENO, &err))
  _sret_normalised_store(out_n)
  _return_normalized_err
}

_gnu_inline _gnu_restrict_access(read_only, 1) _gnu_restrict_access(write_only, 4) _gnu_nonnull(1, 4)
_sppc_api int sppc_stderr_write(char const *restrict buffer, const size_t size, const size_t count,
  ssize_t *restrict out_n) {
  ssize_t err = 0;
  _return_if_guarded_err(&_stderr_mutex, sppc_write(buffer, size, count, STDERR_FILENO, &err))
  _sret_normalised_store(out_n)
  _return_normalized_err
}

// ==================== ASYNC ====================

_gnu_inline_va
_gnu_restrict_access(write_only, 1) _gnu_nonnull(1)
_sppc_api int sppc_async(size_t *handle, void*(*routine)(size_t, uintptr_t const *), const size_t argc, ...) {
  if (argc > GT_MAX_ARGS) { return E2BIG; }

  va_list ap;
  va_start(ap, argc);

  const auto task = gt_spawn((gt_entry_fn)routine);
  if (!task) {
    va_end(ap);
    return ENOMEM;
  }

  task->argc = argc;
  for (size_t i = 0; i < argc; ++i) {
    task->argv[i] = va_arg(ap, uintptr_t);
  }

  va_end(ap);
  *handle = (size_t)task;
  return 0;
}

_gnu_inline
_sppc_api void* sppc_await(const size_t handle) {
  _extract_err gt_await((gt_task*)handle);
  _return_pointer
}

#pragma GCC diagnostic pop
