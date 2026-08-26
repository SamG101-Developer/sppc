#include <sppc/sppc.h>

pthread_mutex_t _stdin_mutex;
pthread_mutex_t _stdout_mutex;
pthread_mutex_t _stderr_mutex;

_Thread_local gt_task *gt_task_pool = NULL;
_Thread_local int gt_task_free = 0;
_Thread_local gt_task *gt_ready_head = NULL;
_Thread_local gt_task *gt_ready_tail = NULL;
_Thread_local gt_task *gt_current = NULL;
_Thread_local gt_task *gt_free_head = NULL;
_Thread_local gt_ctx gt_main_ctx;

pthread_key_t gt_pool_key;
pthread_once_t gt_pool_key_once = PTHREAD_ONCE_INIT;

extern void gt_pool_release(void *pool);
extern void gt_pool_key_init(void);
extern gt_task* gt_pool(void);
extern void gt_enqueue(gt_task *t);
extern gt_task* gt_dequeue(void);
extern size_t gt_handle(gt_task const *t);
extern gt_task* gt_resolve(size_t handle);
extern void* gt_alloc_stack(void);
extern void gt_free_stack(void *p);
extern void gt_task_entry(void);
extern void gt_init(void);
extern gt_task* gt_spawn(gt_entry_fn fn);
extern void gt_yield(void);
extern void* gt_await(gt_task *task);

extern void* _sppc_thread_entry(void *start_routine);

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
extern int sppc_pthread_mutex_clocklock(uint64_t const *restrict mutex, clockid_t clock,
  struct timespec const *restrict duration);
extern int sppc_pthread_mutex_trylock(uint64_t const *restrict mutex);
extern int sppc_pthread_mutex_unlock(uint64_t const *restrict mutex);
extern int sppc_pthread_mutex_destroy(uint64_t const *restrict mutex);
extern int sppc_pthread_once_init(uint64_t *restrict out);
extern int sppc_pthread_once(uint64_t const *restrict once, void (*func)(void));
extern int sppc_pthread_cond_init(uint64_t *restrict out);
extern int sppc_pthread_cond_wait(uint64_t const *restrict cond, uint64_t const *restrict mutex);
extern int sppc_pthread_cond_clockwait(uint64_t const *restrict cond, uint64_t const *restrict mutex,
  clockid_t clock, struct timespec const *restrict duration);
extern int sppc_pthread_cond_signal(uint64_t const *restrict cond);
extern int sppc_pthread_cond_broadcast(uint64_t const *restrict cond);
extern int sppc_pthread_cond_destroy(uint64_t const *restrict cond);
extern int sppc_pthread_rwlock_init(uint64_t *restrict rwlock);
extern int sppc_pthread_rwlock_rdlock(uint64_t const *restrict rwlock);
extern int sppc_pthread_rwlock_tryrdlock(uint64_t const *restrict rwlock);
extern int sppc_pthread_rwlock_clockrdlock(uint64_t const *restrict rwlock, clockid_t clock,
  struct timespec const *restrict duration);
extern int sppc_pthread_rwlock_wrlock(uint64_t const *restrict rwlock);
extern int sppc_pthread_rwlock_trywrlock(uint64_t const *restrict rwlock);
extern int sppc_pthread_rwlock_clockwrlock(uint64_t const *restrict rwlock, clockid_t clock,
  struct timespec const *restrict duration);
extern int sppc_pthread_rwlock_unlock(uint64_t const *restrict rwlock);
extern int sppc_pthread_rwlock_destroy(uint64_t const *restrict rwlock);
extern int sppc_pthread_barrier_init(uint64_t *restrict barrier, uint64_t count);
extern int sppc_pthread_barrier_wait(uint64_t const *restrict barrier);
extern int sppc_pthread_barrier_destroy(uint64_t const *restrict barrier);
extern int sppc_pthread_spin_init(uint64_t *restrict spinlock);
extern int sppc_pthread_spin_lock(uint64_t const *restrict spinlock);
extern int sppc_pthread_spin_trylock(uint64_t const *restrict spinlock);
extern int sppc_pthread_spin_unlock(uint64_t const *restrict spinlock);
extern int sppc_pthread_spin_destroy(uint64_t const *restrict spinlock);
extern void* sppc_malloc(size_t size);
extern void* sppc_aligned_alloc(size_t size, size_t alignment);
extern void* sppc_calloc(size_t num, size_t size);
extern void* sppc_realloc(void *ptr, size_t new_size);
extern void sppc_free(void *ptr);
extern int sppc_memcpy(void *restrict dest, void const *restrict src, size_t size, size_t dest_index,
  size_t src_index);
extern int sppc_memmove(void *restrict dest, void const *restrict src, size_t size);
extern int sppc_memset(void *restrict dest, int value, size_t size, size_t dest_index);
extern int sppc_memcmp(void const *ptr1, void const *ptr2, size_t size, int *restrict out);
extern int sppc_memmem(void const *haystack, size_t haystack_size, void const *needle, size_t needle_size,
  size_t *restrict out);
extern int sppc_read(char *restrict buffer, size_t size, size_t count, int fd,
  ssize_t *restrict out_n);
extern int sppc_write(char const *restrict buffer, size_t size, size_t count, int fd,
  ssize_t *restrict out_n);
extern int sppc_open(char const *restrict path, int flags, mode_t mode, int *restrict out_fd);
extern int sppc_close(int fd);
extern int sppc_stat(char const *restrict path, struct stat *restrict out);
extern int sppc_fstat(int fd, struct stat *restrict out);
extern int sppc_lstat(char const *restrict path, struct stat *restrict out);
extern int sppc_poll(struct pollfd *restrict fds, nfds_t count, int timeout, int *restrict out_n);
extern int sppc_lseek(int fd, off_t offset, int whence, off_t *restrict out_pos);
extern void* sppc_mmap(int fd, size_t length, int prot, int flags, off_t offset);
extern int sppc_memprotect(void *addr, size_t size, int prot);
extern int sppc_munmap(void *restrict addr, size_t const *restrict length);
extern int sppc_pread(int fd, void *restrict buffer, size_t size, size_t count, off_t offset,
  ssize_t *restrict out_n);
extern int sppc_pwrite(int fd, void const *restrict buffer, size_t size, size_t count,
  off_t offset, ssize_t *restrict out_n);
extern int sppc_readv(int fd, struct iovec const *restrict iov, int iov_count, ssize_t *restrict out_n);
extern int sppc_writev(int fd, struct iovec const *restrict iov, int iov_count, ssize_t *restrict out_n);
extern int sppc_access(char const *restrict path, int flags, int *restrict out);
extern int sppc_pipe(int *restrict out_read_fd, int *restrict out_write_fd);
extern int sppc_select(int nfds, fd_set *restrict readfds, fd_set *restrict writefds, fd_set *restrict exceptfds,
  struct timeval *restrict timeout, int *restrict out_n);
extern void sppc_sched_yield(void);
extern int sppc_msync(void *restrict addr, size_t const *restrict length, int flags);
extern int sppc_madvise(void *restrict addr, size_t const *restrict length, int advice);
extern int sppc_dup(int fd, int *restrict out_fd);
extern int sppc_dup2(int fd, int target_fd);
extern void sppc_get_pid(pid_t *restrict out_pid);
extern int sppc_sendfile(int from_fd, int to_fd, off_t *offset, size_t count,
  ssize_t *restrict out_n);
extern int sppc_socket(int domain, int type, int protocol, int *restrict out_fd);
extern int sppc_connect(int fd, struct sockaddr_storage const *restrict storage);
extern int sppc_accept(int fd, struct sockaddr_storage *restrict out_storage, int *restrict out_fd);
extern int sppc_sendto(int fd, char const *data, size_t size,
  struct sockaddr_storage const *restrict storage, ssize_t *restrict out_n);
extern int sppc_recvfrom(int fd, char *buffer, size_t size, struct sockaddr_storage *restrict out_storage,
  ssize_t *restrict out_n);
extern int sppc_send(int fd, char const *data, size_t size, int flags, ssize_t *restrict out_n);
extern int sppc_recv(int fd, char *buffer, size_t size, int flags, ssize_t *restrict out_n);
extern int sppc_shutdown(int fd, int how);
extern int sppc_bind(int fd, struct sockaddr_storage const *restrict storage);
extern int sppc_listen(int fd, int backlog);
extern int sppc_getsockname(int fd, struct sockaddr_storage *restrict out_storage);
extern int sppc_getpeername(int fd, struct sockaddr_storage *restrict out_storage);
extern void sppc_exit(int status);
extern int sppc_signal(pid_t pid, int signal);
extern int sppc_fcntl(int fd, int cmd, ...);
extern int sppc_fsync(int fd);
extern int sppc_fdatasync(int fd);
extern int sppc_truncate(char const *restrict path, off_t length);
extern int sppc_ftruncate(int fd, off_t length);
extern int sppc_getcwd(char *restrict buffer, size_t size);
extern int sppc_chdir(char const *restrict path);
extern int sppc_rename(char const *restrict old_path, char const *restrict new_path);
extern int sppc_mkdir(char const *restrict path, mode_t mode);
extern int sppc_rmdir(char const *restrict path);
extern int sppc_link(char const *restrict target, char const *restrict linkpath);
extern int sppc_symlink(char const *restrict target, char const *restrict linkpath);
extern int sppc_unlink(char const *restrict path);
extern int sppc_readlink(char const *restrict path, char *restrict buffer, size_t buffer_size);
extern int sppc_chmod(char const *restrict path, mode_t mode);
extern int sppc_chown(char const *restrict path, uid_t owner, gid_t group);
extern int sppc_lchown(char const *restrict path, uid_t owner, gid_t group);
extern void sppc_get_ppid(pid_t *restrict out_ppid);
extern int sppc_mlock(const void *addr, size_t size);
extern int sppc_munlock(const void *addr, size_t size);
extern int sppc_clock_gettime(clockid_t clock_id, struct timespec *restrict out_tp);
extern int sppc_clock_nanosleep(clockid_t clock, int flags, struct timespec const *restrict duration);
extern int sppc_utimensat(char const *restrict path, int flags);
extern int sppc_getrandom(size_t size, char *restrict out);
extern int sppc_realpath(char const *restrict path, char *restrict buffer);
extern int sppc_mktemp(char *restrict path, int *restrict out_fd);
extern int sppc_mktemp_dir(char *restrict path);
extern int sppc_statvfs(char const *restrict path, struct statvfs *restrict out);
extern int sppc_copyfile(int fd_in, int fd_out, size_t len, int flags,
  ssize_t *restrict out_n);
extern int sppc_strcpy(char *restrict dest, char const *restrict src);
extern int sppc_strcat(char *restrict dest, char const *restrict src);
extern int sppc_strcmp(char const *str1, char const *str2, bool *restrict out);
extern int sppc_strcasecmp(char const *str1, char const *str2, bool *restrict out);
extern int sppc_strchr(char const *str, char ch, size_t *restrict out_idx);
extern int sppc_strrchr(char const *str, char ch, size_t *restrict out_idx);
extern int sppc_strstr(char const *haystack, char const *needle, size_t *restrict out_idx);
extern int sppc_strrstr(char const *haystack, char const *needle, size_t *restrict out_idx);
extern int sppc_strcasestr(char const *haystack, char const *needle, size_t *restrict out_idx);
extern int sppc_strpbrk(char const *string, char const *accept, size_t *restrict out_idx);
extern void* sppc_strdup(char const *str);
extern int sppc_getenv(char const *restrict key, char *restrict out, size_t size);
extern int sppc_setenv(char const *restrict key, char const *restrict val, bool overwrite);
extern int sppc_unsetenv(char const *restrict key);
extern void sppc_halt(int status);
extern void sppc_abort();
extern void sppc_set_sockaddr_v4(uint8_t const *octets, uint16_t port,
  struct sockaddr_storage *restrict out_storage);
extern void sppc_set_sockaddr_v6(uint16_t const *segments, uint16_t port,
  struct sockaddr_storage *restrict out_storage);
extern void sppc_get_sockaddr_v4(struct sockaddr_storage const *restrict storage, uint8_t *out_octets,
  uint16_t *out_port);
extern void sppc_get_sockaddr_v6(struct sockaddr_storage const *restrict storage, uint16_t *out_segments,
  uint16_t *out_port);
extern void sppc_sockaddr_family(struct sockaddr_storage const *restrict storage, int *restrict out_family);
extern int sppc_setsockopt(int fd, int level, int optname, int const *restrict optval);
extern int sppc_getsockopt(int fd, int level, int optname, int *restrict optval);
extern int sppc_stdin_read(char *restrict buffer, size_t size, size_t count, ssize_t *restrict out_n);
extern int sppc_stdout_write(char const *restrict buffer, size_t size, size_t count,
  ssize_t *restrict out_n);
extern int sppc_stderr_write(char const *restrict buffer, size_t size, size_t count,
  ssize_t *restrict out_n);
extern int sppc_async(size_t *handle, void*(*routine)(size_t, uintptr_t const *), size_t argc, ...);
extern void* sppc_await(size_t handle);
