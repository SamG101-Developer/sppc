#include <sppc/sppc.h>

extern int sppc_init(void);
extern int sppc_cleanup(void);
extern int sppc_pthread_create(void (*start_routine)(void), uint64_t *restrict out);
extern int sppc_pthread_join(uint64_t const *restrict handle);
extern int sppc_pthread_detach(uint64_t const *restrict handle);
extern void sppc_pthread_equal(uint64_t const *handle1, uint64_t const *handle2, uint64_t *restrict out);
extern void sppc_pthread_self(uint64_t *restrict out);
extern int sppc_pthread_mutex_init(uint64_t *restrict out);
extern int sppc_pthread_mutex_init_recursive(uint64_t *restrict out);
extern int sppc_pthread_mutex_lock(uint64_t const *restrict mutex);
extern int sppc_pthread_mutex_clocklock(uint64_t const *restrict mutex, const clockid_t clock,
  struct timespec const *restrict duration);
extern int sppc_pthread_mutex_trylock(uint64_t const *restrict mutex);
extern int sppc_pthread_mutex_unlock(uint64_t const *restrict mutex);
extern int sppc_pthread_mutex_destroy(uint64_t const *restrict mutex);
extern int sppc_pthread_once(void (*func)(void));
extern int sppc_pthread_cond_init(uint64_t *restrict out);
extern int sppc_pthread_cond_wait(uint64_t const *restrict cond, uint64_t const *restrict mutex);
extern int sppc_pthread_cond_clockwait(uint64_t const *restrict cond, uint64_t const *restrict mutex,
  const clockid_t clock, struct timespec const *restrict duration);
extern int sppc_pthread_cond_signal(uint64_t const *restrict cond);
extern int sppc_pthread_cond_broadcast(uint64_t const *restrict cond);
extern int sppc_pthread_cond_destroy(uint64_t const *restrict cond);
extern int sppc_pthread_rwlock_init(uint64_t *restrict rwlock);
extern int sppc_pthread_rwlock_rdlock(uint64_t const *restrict rwlock);
extern int sppc_pthread_rwlock_tryrdlock(uint64_t const *restrict rwlock);
extern int sppc_pthread_rwlock_clockrdlock(uint64_t const *restrict rwlock, const clockid_t clock,
  struct timespec const *restrict duration);
extern int sppc_pthread_rwlock_wrlock(uint64_t const *restrict rwlock);
extern int sppc_pthread_rwlock_trywrlock(uint64_t const *restrict rwlock);
extern int sppc_pthread_rwlock_clockwrlock(uint64_t const *restrict rwlock, const clockid_t clock,
  struct timespec const *restrict duration);
extern int sppc_pthread_rwlock_unlock(uint64_t const *restrict rwlock);
extern int sppc_pthread_rwlock_destroy(uint64_t const *restrict rwlock);
extern int sppc_pthread_barrier_init(uint64_t *restrict barrier, const uint64_t count);
extern int sppc_pthread_barrier_wait(uint64_t const *restrict barrier);
extern int sppc_pthread_barrier_destroy(uint64_t const *restrict barrier);
extern int sppc_pthread_spin_init(uint64_t *restrict spinlock);
extern int sppc_pthread_spin_lock(uint64_t const *restrict spinlock);
extern int sppc_pthread_spin_trylock(uint64_t const *restrict spinlock);
extern int sppc_pthread_spin_unlock(uint64_t const *restrict spinlock);
extern int sppc_pthread_spin_destroy(uint64_t const *restrict spinlock);
extern void* sppc_malloc(const size_t size);
extern void* sppc_aligned_alloc(const size_t size, const size_t alignment);
extern void* sppc_calloc(const size_t num, const size_t size);
extern void* sppc_realloc(void *ptr, const size_t new_size);
extern void sppc_free(void *ptr);
extern int sppc_memcpy(void *restrict dest, void const *restrict src, const size_t size, const size_t dest_index,
  const size_t src_index);
extern int sppc_memmove(void *restrict dest, void const *restrict src, const size_t size);
extern int sppc_memset(void *restrict dest, const int value, const size_t size, const size_t dest_index);
extern int sppc_memcmp(void const *ptr1, void const *ptr2, const size_t size, int *restrict out);
extern int sppc_memmem(void const *haystack, const size_t haystack_size, void const *needle, const size_t needle_size,
  size_t *restrict out);
extern int sppc_read(char *restrict buffer, const size_t size, const size_t count, const int fd,
  ssize_t *restrict out_n);
extern int sppc_write(char const *restrict buffer, const size_t size, const size_t count, const int fd,
  ssize_t *restrict out_n);
extern int sppc_open(char const *restrict path, const int flags, const mode_t mode, int *restrict out_fd);
extern int sppc_close(const int fd);
extern int sppc_stat(char const *restrict path, struct stat *restrict out);
extern int sppc_fstat(const int fd, struct stat *restrict out);
extern int sppc_lstat(char const *restrict path, struct stat *restrict out);
extern int sppc_poll(struct pollfd *restrict fds, const nfds_t count, const int timeout, int *restrict out_n);
extern int sppc_lseek(const int fd, const off_t offset, const int whence, off_t *restrict out_pos);
extern int sppc_mmap(const int fd, const size_t length, const int prot, const int flags, const off_t offset,
  void *restrict out_addr);
extern int sppc_memprotect(void *addr, const size_t size, const int prot);
extern int sppc_munmap(void *restrict addr, size_t const *restrict length);
extern int sppc_pread(const int fd, void *restrict buffer, const size_t size, const size_t count, const off_t offset,
  ssize_t *restrict out_n);
extern int sppc_pwrite(const int fd, void const *restrict buffer, const size_t size, const size_t count,
  const off_t offset, ssize_t *restrict out_n);
extern int sppc_readv(const int fd, struct iovec const *restrict iov, const int iov_count, ssize_t *restrict out_n);
extern int sppc_writev(const int fd, struct iovec const *restrict iov, const int iov_count, ssize_t *restrict out_n);
extern int sppc_access(char const *restrict path, const int flags, int *restrict out);
extern int sppc_pipe(int *restrict out_read_fd, int *restrict out_write_fd);
extern int sppc_select(const int nfds, fd_set *restrict readfds, fd_set *restrict writefds, fd_set *restrict exceptfds,
  struct timeval *restrict timeout, int *restrict out_n);
extern void sppc_sched_yield(void);
extern int sppc_msync(void *restrict addr, size_t const *restrict length, const int flags);
extern int sppc_madvise(void *restrict addr, size_t const *restrict length, const int advice);
extern int sppc_dup(const int fd, int *restrict out_fd);
extern int sppc_dup2(const int fd, const int target_fd);
extern void sppc_get_pid(pid_t *restrict out_pid);
extern int sppc_sendfile(const int from_fd, const int to_fd, const off_t *offset, const size_t count);
extern int sppc_socket(const int domain, const int type, const int protocol, int *restrict out_fd);
extern int sppc_connect(const int fd, struct sockaddr_storage const *restrict storage);
extern int sppc_accept(const int fd, struct sockaddr_storage *restrict out_storage, int *restrict out_fd);
extern int sppc_sendto(const int fd, char const *data, const size_t size,
  struct sockaddr_storage const *restrict storage, ssize_t *restrict out_n);
extern int sppc_recvfrom(const int fd, char *buffer, const size_t size, struct sockaddr_storage *restrict out_storage,
  ssize_t *restrict out_n);
extern int sppc_send(const int fd, char const *data, const size_t size, const int flags, ssize_t *restrict out_n);
extern int sppc_recv(const int fd, char *buffer, const size_t size, const int flags, ssize_t *restrict out_n);
extern int sppc_shutdown(const int fd, const int how);
extern int sppc_bind(const int fd, struct sockaddr_storage const *restrict storage);
extern int sppc_listen(const int fd, const int backlog);
extern int sppc_getsockname(const int fd, struct sockaddr_storage *restrict out_storage);
extern int sppc_getpeername(const int fd, struct sockaddr_storage *restrict out_storage);
extern void sppc_exit(const int status);
extern int sppc_signal(const pid_t pid, const int signal);
extern int sppc_fcntl(const int fd, const int cmd, ...);
extern int sppc_fsync(const int fd);
extern int sppc_fdatasync(const int fd);
extern int sppc_truncate(char const *restrict path, const off_t length);
extern int sppc_ftruncate(const int fd, const off_t length);
extern int sppc_getcwd(char *restrict buffer, const size_t size);
extern int sppc_chdir(char const *restrict path);
extern int sppc_rename(char const *restrict old_path, char const *restrict new_path);
extern int sppc_mkdir(char const *restrict path, const mode_t mode);
extern int sppc_rmdir(char const *restrict path);
extern int sppc_link(char const *restrict target, char const *restrict linkpath);
extern int sppc_symlink(char const *restrict target, char const *restrict linkpath);
extern int sppc_unlink(char const *restrict path);
extern int sppc_readlink(char const *restrict path, char *restrict buffer, const size_t buffer_size);
extern int sppc_chmod(char const *restrict path, const mode_t mode);
extern int sppc_chown(char const *restrict path, const uid_t owner, const gid_t group);
extern int sppc_lchown(char const *restrict path, const uid_t owner, const gid_t group);
extern void sppc_get_ppid(pid_t *restrict out_ppid);
extern int sppc_mlock(const void *addr, const size_t size);
extern int sppc_munlock(const void *addr, const size_t size);
extern int sppc_clock_gettime(const clockid_t clock_id, struct timespec *restrict out_tp);
extern int sppc_clock_nanosleep(const clockid_t clock, const int flags, struct timespec const *restrict duration);
extern int sppc_utimensat(char const *restrict path, const int flags);
extern int sppc_getrandom(const size_t size, char *restrict out);
extern int sppc_realpath(char const *restrict path, char *restrict buffer);
extern int sppc_mktemp(char *restrict path, int *restrict out_fd);
extern int sppc_mktemp_dir(char *restrict path);
extern int sppc_statvfs(char const *restrict path, struct statvfs *restrict out);
extern int sppc_copyfile(const int fd_in, const int fd_out, const size_t len, const int flags);
extern int sppc_strcpy(char *restrict dest, char const *restrict src);
extern int sppc_strcat(char *restrict dest, char const *restrict src);
extern int sppc_strcmp(char const *str1, char const *str2, bool *restrict out);
extern int sppc_strcasecmp(char const *str1, char const *str2, bool *restrict out);
extern int sppc_strchr(char const *str, const char ch, size_t *restrict out_idx);
extern int sppc_strrchr(char const *str, const char ch, size_t *restrict out_idx);
extern int sppc_strstr(char const *haystack, char const *needle, size_t *restrict out_idx);
extern int sppc_strrstr(char const *haystack, char const *needle, size_t *restrict out_idx);
extern int sppc_strcasestr(char const *haystack, char const *needle, size_t *restrict out_idx);
extern int sppc_strpbrk(char const *string, char const *accept, size_t *restrict out_idx);
extern void* sppc_strdup(char const *str);
extern int sppc_getenv(char const *restrict key, char *restrict out);
extern int sppc_setenv(char const *restrict key, char const *restrict val, const bool overwrite);
extern int sppc_unsetenv(char const *restrict key);
extern void sppc_halt(const int status);
extern void sppc_abort();
extern int sppc_set_sockaddr_v4(uint8_t const *octets, const uint16_t port,
  struct sockaddr_storage *restrict out_storage);
extern int sppc_set_sockaddr_v6(uint16_t const *segments, const uint16_t port,
  struct sockaddr_storage *restrict out_storage);
extern int sppc_get_sockaddr_v4(struct sockaddr_storage const *restrict storage, uint8_t *out_octets,
  uint16_t *out_port);
extern int sppc_get_sockaddr_v6(struct sockaddr_storage const *restrict storage, uint16_t *out_segments,
  uint16_t *out_port);
extern void sppc_sockaddr_family(struct sockaddr_storage const *restrict storage, int *restrict out_family);
extern int sppc_setsockopt(const int fd, const int level, const int optname, int const *restrict optval);
extern int sppc_getsockopt(const int fd, const int level, const int optname, int *restrict optval);
extern int sppc_stdin_read(char *restrict buffer, const size_t size, const size_t count, ssize_t *restrict out_n);
extern int sppc_stdout_write(char const *restrict buffer, const size_t size, const size_t count,
  ssize_t *restrict out_n);
extern int sppc_stderr_write(char const *restrict buffer, const size_t size, const size_t count,
  ssize_t *restrict out_n);
extern int sppc_async(size_t *handle, void*(*routine)(size_t, uintptr_t const *), const size_t argc, ...);
extern void* sppc_await(const size_t handle);
