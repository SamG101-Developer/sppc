/**
 * Safety guarantees from S++
 *   * All "out" integers will be 0 by default.
 *   * All pointers will be non-null, initialised.
 */

#pragma once
#include <sppc/macros.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/poll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/uio.h>

typedef int64_t ssize_t;
typedef int64_t off_t;

#ifndef DEBUG_BUILD // Set from cmake.
#define DEBUG_BUILD 0
#endif

SPPC_API int c_init(void);
SPPC_API int c_cleanup(void);

SPPC_API int c_pthread_create(void(*start_routine)(void), uint64_t *restrict out);
SPPC_API int c_pthread_join(uint64_t const *restrict handle);
SPPC_API int c_pthread_detach(uint64_t const *restrict handle);
SPPC_API int c_pthread_equal(uint64_t const *handle1, uint64_t const *handle2, uint64_t *restrict out);
SPPC_API int c_pthread_self(uint64_t *restrict out);

SPPC_API void c_sched_yield(void);

SPPC_API int c_pthread_mutex_init(uint64_t *restrict out);
SPPC_API int c_pthread_mutex_init_recursive(uint64_t *restrict out);
SPPC_API int c_pthread_mutex_lock(uint64_t const *restrict mutex);
SPPC_API int c_pthread_mutex_clocklock(uint64_t const *restrict mutex, clockid_t clock, struct timespec const *restrict duration);
SPPC_API int c_pthread_mutex_trylock(uint64_t const *restrict mutex);
SPPC_API int c_pthread_mutex_unlock(uint64_t const *restrict mutex);
SPPC_API int c_pthread_mutex_destroy(uint64_t const *restrict mutex);

SPPC_API int c_pthread_once(void(*func)(void));

SPPC_API int c_pthread_cond_init(uint64_t *restrict out);
SPPC_API int c_pthread_cond_wait(uint64_t const *restrict cond, uint64_t const *restrict mutex);
SPPC_API int c_pthread_cond_timedwait(uint64_t const *restrict cond, uint64_t const *restrict mutex, clockid_t clock, struct timespec const *restrict duration);
SPPC_API int c_pthread_cond_signal(uint64_t const *restrict cond);
SPPC_API int c_pthread_cond_broadcast(uint64_t const *restrict cond);
SPPC_API int c_pthread_cond_destroy(uint64_t const *restrict cond);

SPPC_API int c_pthread_rwlock_init(uint64_t *restrict rwlock);
SPPC_API int c_pthread_rwlock_rdlock(uint64_t const *restrict rwlock);
SPPC_API int c_pthread_rwlock_tryrdlock(uint64_t const *restrict rwlock);
SPPC_API int c_pthread_rwlock_clockrdlock(uint64_t const *restrict rwlock, clockid_t clock, struct timespec const *restrict duration);
SPPC_API int c_pthread_rwlock_wrlock(uint64_t const *restrict rwlock);
SPPC_API int c_pthread_rwlock_trywrlock(uint64_t const *restrict rwlock);
SPPC_API int c_pthread_rwlock_clockwrlock(uint64_t const *restrict rwlock, clockid_t clock, struct timespec const *restrict duration);
SPPC_API int c_pthread_rwlock_unlock(uint64_t const *restrict rwlock);
SPPC_API int c_pthread_rwlock_destroy(uint64_t const *restrict rwlock);

SPPC_API int c_pthread_barrier_init(uint64_t *restrict barrier, uint64_t count);
SPPC_API int c_pthread_barrier_wait(uint64_t const *restrict barrier);
SPPC_API int c_pthread_barrier_destroy(uint64_t const *restrict barrier);

SPPC_API int c_pthread_spin_init(uint64_t *restrict spinlock);
SPPC_API int c_pthread_spin_lock(uint64_t const *restrict spinlock);
SPPC_API int c_pthread_spin_trylock(uint64_t const *restrict spinlock);
SPPC_API int c_pthread_spin_unlock(uint64_t const *restrict spinlock);
SPPC_API int c_pthread_spin_destroy(uint64_t const *restrict spinlock);

SPPC_API int c_read(char *restrict buffer, size_t size, size_t count, int fd, ssize_t *restrict out_n);
SPPC_API int c_write(char const *restrict buffer, size_t size, size_t count, int fd, ssize_t *restrict out_n);
SPPC_API int c_stdin_read(char *restrict buffer, size_t size, size_t count, ssize_t *restrict out_n);
SPPC_API int c_stdout_write(char const *restrict buffer, size_t size, size_t count, ssize_t *restrict out_n);
SPPC_API int c_stderr_write(char const *restrict buffer, size_t size, size_t count, ssize_t *restrict out_n);
SPPC_API int c_open(char const *restrict path, int flags, mode_t mode, int *restrict out_fd);
SPPC_API int c_close(int fd);
SPPC_API int c_flush(int fd);
SPPC_API int c_flush_data(int fd);
SPPC_API int c_seek(int fd, off_t offset, int whence, off_t *restrict out_pos);
SPPC_API int c_tell(int fd, off_t *restrict out_pos);
SPPC_API int c_truncate(int fd, off_t length);
SPPC_API int c_lock_ex(int fd, bool non_blocking);
SPPC_API int c_lock_sh(int fd, bool non_blocking);
SPPC_API int c_unlock(int fd);
SPPC_API int c_stat(int fd, struct stat *restrict out);
SPPC_API int c_dup(int fd, int *restrict out_fd);
SPPC_API int c_dup_into(int fd, int target_fd);
SPPC_API int c_pipe(int *restrict out_read_fd, int *restrict out_write_fd);
SPPC_API int c_get_flags(int fd, int *restrict out);
SPPC_API int c_set_flags(int fd, int flags);
SPPC_API int c_get_status(int fd, int *restrict out);
SPPC_API int c_set_status(int fd, int flags);
SPPC_API int c_poll(struct pollfd *restrict fds, nfds_t count, struct timespec const *restrict duration, int *restrict out_n);
SPPC_API int c_readv(int fd, struct iovec const *restrict iov, int iov_count, ssize_t *restrict out_n);
SPPC_API int c_writev(int fd, struct iovec const *restrict iov, int iov_count, ssize_t *restrict out_n);
SPPC_API int c_pread(int fd, void *restrict buffer, size_t size, size_t count, off_t offset, ssize_t *restrict out_n);
SPPC_API int c_pwrite(int fd, void const *restrict buffer, size_t size, size_t count, off_t offset, ssize_t *restrict out_n);
SPPC_API int c_mmap(int fd, size_t length, int prot, int flags, off_t offset, void *restrict out_addr, size_t *restrict out_length);
SPPC_API int c_munmap(void *restrict addr, size_t const *restrict length);
SPPC_API int c_msync(void *restrict addr, size_t const *restrict length, int flags);
SPPC_API int c_madvise(void *restrict addr, size_t const *restrict length, int advice);
SPPC_API int c_isatty(int fd, bool *restrict out);

SPPC_API int c_exists(char const *restrict path, bool *restrict out);
SPPC_API int c_is_file(char const *restrict path, bool *restrict out);
SPPC_API int c_is_dir(char const *restrict path, bool *restrict out);
SPPC_API int c_is_symlink(char const *restrict path, bool *restrict out);
SPPC_API int c_filesize(char const *restrict path, off_t *restrict out);
SPPC_API int c_remove(char const *restrict path);
SPPC_API int c_rename(char const *restrict old_path, char const *restrict new_path);
SPPC_API int c_mkdir(char const *restrict path, mode_t mode);
SPPC_API int c_rmdir(char const *restrict path);
SPPC_API int c_chmod(char const *restrict path, mode_t mode);
SPPC_API int c_fsstat(char const *restrict path, struct stat *restrict out, bool follow_symlink);
SPPC_API int c_chown(char const *restrict path, uid_t owner, gid_t group, bool follow_symlink);
SPPC_API int c_access(char const *restrict path, int flags);
SPPC_API int c_touch(char const *restrict path, int flags);
SPPC_API int c_realpath(char const *restrict path, char *restrict buffer);
SPPC_API int c_hardlink(char const *restrict target, char const *restrict linkpath);
SPPC_API int c_symlink(char const *restrict target, char const *restrict linkpath);
SPPC_API int c_readlink(char const *restrict path, char *restrict buffer, size_t buffer_size);
SPPC_API int c_mktemp(char *restrict path, int *restrict out_fd);
SPPC_API int c_mktemp_dir(char *restrict path);
SPPC_API int c_statvfs(char const *restrict path, struct statvfs *restrict out);
SPPC_API int c_copyfile(int fd_in, int fd_out, size_t len, int flags);
SPPC_API int c_getcwd(char *restrict buffer, size_t size);
SPPC_API int c_chdir(char const *restrict path);

SPPC_API void* c_malloc(size_t size);
SPPC_API void* c_aligned_alloc(size_t size, size_t alignment);
SPPC_API void* c_calloc(size_t num, size_t size);
SPPC_API void* c_realloc(void *ptr, size_t new_size);
SPPC_API void c_free(void *ptr);

SPPC_API int c_memcpy(void *restrict dest, void const *restrict src, size_t size);
SPPC_API int c_memmove(void *restrict dest, void const *restrict src, size_t size);
SPPC_API int c_memset(void *dest, int value, size_t size);
SPPC_API int c_memcmp(void const *ptr1, void const *ptr2, size_t size, int *restrict out);
SPPC_API int c_memcmpconst(void const *ptr1, void const *ptr2, size_t size, int *restrict out);
SPPC_API int c_memmem(void const *haystack, size_t haystack_size, void const *needle, size_t needle_size, size_t *restrict out);
SPPC_API int c_mlock(const void *addr, size_t size);
SPPC_API int c_munlock(const void *addr, size_t size);
SPPC_API int c_memprotect(void *addr, size_t size, int prot);

SPPC_API int c_strcpy(char *restrict dest, char const *restrict src);
SPPC_API int c_strcat(char *restrict dest, char const *restrict src);
SPPC_API int c_strcmp(char const *str1, char const *str2, bool *restrict out);
SPPC_API int c_strcasecmp(char const *str1, char const *str2, bool *restrict out);
SPPC_API int c_strchr(char const *str, char ch, size_t *restrict out_idx);
SPPC_API int c_strrchr(char const *str, char ch, size_t *restrict out_idx);
SPPC_API int c_strstr(char const *haystack, char const *needle, size_t *restrict out_idx);
SPPC_API int c_strrstr(char const *haystack, char const *needle, size_t *restrict out_idx);
SPPC_API int c_strcasestr(char const *haystack, char const *needle, size_t *restrict out_idx);
SPPC_API int c_strpbrk(char const *string, char const *accept, size_t *restrict out_idx);
SPPC_API void* c_strdup(char const *str);

SPPC_API int c_get_pid(pid_t *restrict out_pid);
SPPC_API int c_get_ppid(pid_t *restrict out_ppid);
SPPC_API int c_getenv(char const *restrict key, char *restrict out);
SPPC_API int c_setenv(char const *restrict key, char const *restrict val, bool overwrite);
SPPC_API int c_unsetenv(char const *restrict key);
// SPPC_API int c_exec(char const *restrict path, char const *const *restrict argv, char const *const *restrict envp, int stdin_fd, int stdout_fd, int stderr_fd, pid_t *restrict out_pid);
SPPC_API int c_signal(pid_t pid, int signal);
SPPC_API int c_is_running(pid_t pid);
SPPC_API SPPC_NORETURN void c_exit(int status);
SPPC_API SPPC_NORETURN void c_exit_clean(int status);
SPPC_API SPPC_NORETURN void c_abort();

SPPC_API int c_set_sockaddr_v4(uint8_t const *octets, uint16_t port, struct sockaddr_storage *restrict out_storage);
SPPC_API int c_set_sockaddr_v6(uint16_t const *segments, uint16_t port, struct sockaddr_storage *restrict out_storage);
SPPC_API int c_get_sockaddr_v4(struct sockaddr_storage const *restrict storage, uint8_t *out_octets, uint16_t *out_port);
SPPC_API int c_get_sockaddr_v6(struct sockaddr_storage const *restrict storage, uint16_t *out_segments, uint16_t *out_port);
SPPC_API int c_sockaddr_family(struct sockaddr_storage const *restrict storage, int *restrict out_family);
SPPC_API int c_socket(int domain, int type, int protocol, int *restrict out_fd);
SPPC_API int c_shutdown(int fd, int how);
SPPC_API int c_connect(int fd, struct sockaddr_storage const *restrict storage);
SPPC_API int c_bind(int fd, struct sockaddr_storage const *restrict storage);
SPPC_API int c_listen(int fd, int backlog);
SPPC_API int c_accept(int fd, struct sockaddr_storage *restrict out_storage, int *restrict out_fd);
SPPC_API int c_send(int fd, char const *data, size_t size, int flags, ssize_t *restrict out_n);
SPPC_API int c_recv(int fd, char *buffer, size_t size, int flags, ssize_t *restrict out);
SPPC_API int c_sendto(int fd, char const *data, size_t size, struct sockaddr_storage const *restrict storage, ssize_t *restrict out_n);
SPPC_API int c_recvfrom(int fd, char *buffer, size_t size, struct sockaddr_storage *restrict out_storage, ssize_t *restrict out_n);
SPPC_API int c_set_nonblocking(int fd, bool non_blocking);
SPPC_API int c_set_keepalive(int fd, bool keepalive);
SPPC_API int c_set_linger(int fd, bool on, struct timeval const *restrict linger);
SPPC_API int c_set_nodelay(int fd, bool nodelay);
SPPC_API int c_set_ttl(int fd, int ttl);
SPPC_API int c_set_broadcast(int fd, bool broadcast);
SPPC_API int c_set_recv_timeout(int fd, struct timeval const *restrict timeout);
SPPC_API int c_set_send_timeout(int fd, struct timeval const *restrict timeout);
SPPC_API int c_get_keepalive(int fd, bool *restrict out_keepalive);
SPPC_API int c_get_linger(int fd, bool *restrict out_on, struct timeval *restrict out_linger);
SPPC_API int c_get_nodelay(int fd, bool *restrict out_nodelay);
SPPC_API int c_get_ttl(int fd, int *restrict out_ttl);
SPPC_API int c_get_broadcast(int fd, bool *restrict out_broadcast);
SPPC_API int c_get_recv_timeout(int fd, struct timeval *restrict out_timeout);
SPPC_API int c_get_send_timeout(int fd, struct timeval *restrict out_timeout);
SPPC_API int c_getsockname(int fd, struct sockaddr_storage *restrict out_storage);
SPPC_API int c_getpeername(int fd, struct sockaddr_storage *restrict out_storage);

SPPC_API int c_clock_gettime(clockid_t clock_id, struct timespec *restrict out_tp);
SPPC_API int c_clock_nanosleep(clockid_t clock, int flags, struct timespec const *restrict duration);

SPPC_API int c_prngseed(uint64_t seed);
SPPC_API int c_prngreset(void);
SPPC_API int c_prngbytes(size_t size, char *restrict out);
SPPC_API int c_prngu32(uint32_t *restrict out);
SPPC_API int c_prngu64(uint64_t *restrict out);
SPPC_API int c_prngdouble(double *restrict out);
SPPC_API int c_prngbetween(uint64_t min, uint64_t max, uint64_t *restrict out);
SPPC_API int c_csprngbytes(size_t size, char *restrict out);
SPPC_API int c_csprngu32(uint32_t *restrict out);
SPPC_API int c_csprngu64(uint64_t *restrict out);
