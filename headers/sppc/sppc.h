/**
 * Most functions being wrapped are direct kernel functions, bypassing "libc" everywhere. The functions include safety
 * guarantees to avoid crashes. LibC functions are mostly the allocator functions.
 *
 * TODO:
 *  - File mount & watching implementations.
 *  - Time format
 *  - thread_once?
 */

#pragma once
#include <sppc/macros.h>
#include <sppc/structs.h>

/**
 * A boot function that runs at program start, configuring any C features that might cause strange errors later on. For
 * example, we change some default behaviour to set "errno" rather than killing the process, as this is much better for
 * S++ to interface with.
 */
SPPC_API void init_c(void);
SPPC_API void cleanup_c(void);

/**
 * https://man7.org/linux/man-pages/man2/read.2.html
 * A low level function to read data from a stream to a buffer. The total size of data read is determined by multiplying
 * the size of each element by the number of elements to read. The stream is specified by its file descriptor.
 * @param[out] buffer The buffer to read data into.
 * @param[in] size The size of each element to read, in bytes.
 * @param[in] count The number of elements to read.
 * @param[in] fd The file descriptor of the input stream to read data from.
 * @param[out] out The total number of bytes successfully read from the stream.
 * @return 0 on success, -1 on failure.
 */
SPPC_API int fd_read(void *restrict buffer, size_t size, size_t count, int fd, ssize_t *restrict out);

/**
 * https://man7.org/linux/man-pages/man2/write.2.html
 * A low level function to write data from a buffer to a stream. The total size of data written is determined by
 * multiplying the size of each element by the number of elements to write. The stream is specified by its file
 * descriptor.
 * @param[in] buffer The buffer to write data from.
 * @param[in] size The size of each element to write, in bytes.
 * @param[in] count The number of elements to write.
 * @param[in] fd The file descriptor of the output stream to write data to.
 * @param[out] out The total number of bytes successfully written to the stream.
 * @return 0 on success, -1 on failure.
 */
SPPC_API int fd_write(void const *restrict buffer, size_t size, size_t count, int fd, ssize_t *restrict out);

/**
 * https://man7.org/linux/man-pages/man2/open.2.html
 * A low level function to open a file and return its file descriptor. The file is specified by its name, and the mode
 * and flags determine how the file is opened. The mode is only used when creating a new file, and specifies the
 * permissions to use for the new file. The flags specify how the file should be opened (e.g., read-only, write-only,
 * etc.).
 * @param[in] path The name of the file to open.
 * @param[in] flags The flags to open the file with (e.g., O_RDONLY for read-only, O_WRONLY for write-only).
 * @param[in] mode The permissions to use when creating a new file (e.g., 0644).
 * @param[out] out The file descriptor of the opened file.
 * @return 0 on success, -1 on failure.
 */
SPPC_API int fd_open(char const *restrict path, int flags, mode_t mode, int *restrict out);

/**
 * https://man7.org/linux/man-pages/man2/close.2.html
 * A low level function to close a file descriptor, releasing any resources associated with it. The file descriptor is
 * specified by its integer value. After a file descriptor is closed, it should not be used again, as it may be reused
 * for a different file or resource. Closing a file descriptor that is already closed or invalid will result in an
 * error.
 * @param[in] fd The file descriptor of the file to close.
 * @return 0 on success, -1 on failure.
 */
SPPC_API int fd_close(int fd);

SPPC_API int fd_flush(int fd);

SPPC_API int fd_flush_data(int fd);

SPPC_API int fd_seek(int fd, off_t offset, int whence, off_t *restrict out);

SPPC_API int fd_tell(int fd, off_t *restrict out);

SPPC_API int fd_truncate(int fd, off_t length);

SPPC_API int fd_lock_ex(int fd, bool non_blocking); // locks are per process not per thread

SPPC_API int fd_lock_sh(int fd, bool non_blocking); // locks are per process not per thread

SPPC_API int fd_unlock(int fd); // locks are per process not per thread

SPPC_API int fd_stat(int fd, fd_stat_t *out);

SPPC_API int fd_dup(int fd, int *restrict out);

SPPC_API int fd_dup_into(int fd, int target_fd);

SPPC_API int fd_pipe(int *restrict out_read_fd, int *restrict out_write_fd);

SPPC_API int fd_get_flags(int fd, int *restrict out);

SPPC_API int fd_set_flags(int fd, int flags);

SPPC_API int fd_get_status(int fd, int *restrict out);

SPPC_API int fd_set_status(int fd, int flags);

SPPC_API int fd_poll(fd_poll_t *restrict fds, nfds_t count, int timeout_ms, int *restrict out);

SPPC_API int fd_readv(int fd, fd_iovec_t const *restrict iov, int iov_count, ssize_t *restrict out);

SPPC_API int fd_writev(int fd, fd_iovec_t const *restrict iov, int iov_count, ssize_t *restrict out);

SPPC_API int fd_pread(int fd, void *restrict buffer, size_t size, size_t count, off_t offset, ssize_t *restrict out);

SPPC_API int fd_pwrite(int fd, void const *restrict buffer, size_t size, size_t count, off_t offset, ssize_t *restrict out);

SPPC_API int fd_mmap(int fd, size_t length, int prot, int flags, off_t offset, fd_mmap_t *restrict out);

SPPC_API int fd_munmap(fd_mmap_t *restrict mmap);

SPPC_API int fd_msync(fd_mmap_t const *restrict mmap, size_t length, int flags);

SPPC_API int fd_madvise(fd_mmap_t const *restrict mmap, size_t length, int advice);

SPPC_API int fs_exists(char const *restrict path);

SPPC_API int fs_is_file(char const *restrict path);

SPPC_API int fs_is_dir(char const *restrict path);

SPPC_API int fs_is_symlink(char const *restrict path);

SPPC_API int fs_filesize(char const *restrict path, uint64_t *restrict out);

SPPC_API int fs_remove(char const *restrict path);

SPPC_API int fs_rename(char const *restrict old_path, char const *restrict new_path);

SPPC_API int fs_mkdir(char const *restrict path, mode_t mode);

SPPC_API int fs_rmdir(char const *restrict path);

SPPC_API int fs_chmod(char const *restrict path, mode_t mode);

SPPC_API int fs_symlink_target(char const *restrict path, char *restrict buffer);

SPPC_API int fs_stat(char const *restrict path, fd_stat_t *restrict out, bool follow_symlink);

SPPC_API int fs_chown(char const *restrict path, uid_t owner, gid_t group, bool follow_symlink);

SPPC_API int fs_access(char const *restrict path, int flags);

SPPC_API int fs_touch(char const *restrict path, mode_t mode);

SPPC_API int fs_realpath(char const *restrict path, char *restrict buffer);

// SPPC_API int fs_readdir(char const *restrict path, ...); ???

SPPC_API int fs_hardlink(char const *restrict target, char const *restrict linkpath);

SPPC_API int fs_symlink(char const *restrict target, char const *restrict linkpath);

SPPC_API int fs_readlink(char const *restrict path, char *restrict buffer, size_t buffer_size);

SPPC_API int fs_mktemp(char const *restrict path, fs_temp_t *restrict out);

SPPC_API int fs_mktemp_dir(char const *restrict path, fs_temp_t *restrict out);

SPPC_API int fs_statvfs(char const *restrict path, fs_statvfs_t *restrict out);

// SPPC_API int fs_is_mount(char const *restrict path, bool *restrict out);
//
// SPPC_API int fs_watch_create(void);
//
// SPPC_API int fs_watch_add(int watch_fd, char const *restrict path, uint32_t mask);
//
// SPPC_API int fs_watch_remove(int watch_fd, char const *restrict path);
//
// SPPC_API int fs_watch_poll(int watch_fd, fs_watch_events_t *restrict out, int timeout_ms);
//
// SPPC_API int fs_watch_events_free(fs_watch_events_t *restrict events);
//
// SPPC_API int fs_watch_close(int watch_fd);

SPPC_API void* mm_malloc(size_t size);

SPPC_API void* mm_alloc_aligned(size_t size, size_t alignment);

SPPC_API void* mm_calloc(size_t num, size_t size);

SPPC_API void* mm_realloc(void *ptr, size_t new_size);

SPPC_API void mm_free(void *ptr);

SPPC_API int mm_mem_copy(void *restrict dest, void const *restrict src, size_t size);

SPPC_API int mm_mem_move(void *restrict dest, void const *restrict src, size_t size);

SPPC_API int mm_mem_set(void *dest, int value, size_t size);

SPPC_API int mm_mem_zero(void *dest, size_t size);

SPPC_API int mm_mem_cmp(void const *ptr1, void const *ptr2, size_t size, int *restrict out);

SPPC_API int mm_mem_cmp_const(void const *ptr1, void const *ptr2, size_t size, int *restrict out);

SPPC_API int mm_mem_find(void const *haystack, size_t haystack_size, void const *needle, size_t needle_size, size_t *restrict out);

SPPC_API void* mm_mem_map(size_t size);

SPPC_API int mm_mem_unmap(void *addr, size_t size);

SPPC_API int mm_mem_lock(const void *addr, size_t size);

SPPC_API int mm_mem_unlock(const void *addr, size_t size);

SPPC_API int mm_mem_protect(void *addr, size_t size, int prot);

SPPC_API int st_str_len(char const *restrict str, size_t max_len, size_t *restrict out);

SPPC_API int st_str_cpy(char *restrict dest, size_t dest_size, char const *restrict src, size_t *restrict out);

SPPC_API int st_str_cat(char *restrict dest, size_t dest_size, char const *restrict src, size_t *restrict out);

SPPC_API int st_str_cmp(char const *str1, char const *str2, int *restrict out);

SPPC_API int st_str_case_cmp(char const *str1, char const *str2, int *restrict out);

SPPC_API int st_str_chr(char const *str, char ch, int *restrict out);

SPPC_API int st_str_rchr(char const *str, char ch, int *restrict out);

SPPC_API int st_str_str(char const *haystack, char const *needle, int *restrict out);

SPPC_API int st_str_case_str(char const *haystack, char const *needle, int *restrict out);

SPPC_API int st_str_pbrk(char const *string, char const *accept, int *restrict out);

SPPC_API void* st_str_dup(char const *str);

SPPC_API int so_socket(int domain, int type, int protocol, int *restrict out);

SPPC_API int so_close(int socket_fd);

SPPC_API int so_shutdown(int socket_fd, int how);

SPPC_API int so_connect(int socket_fd, so_addr_t const *restrict addr);

SPPC_API int so_bind(int socket_fd, so_addr_t const *restrict addr);

SPPC_API int so_listen(int socket_fd, int backlog);

SPPC_API int so_accept(int socket_fd, so_addr_t *restrict addr, int *restrict out);

SPPC_API int so_send(int socket_fd, char const *data, size_t size, int flags, ssize_t *restrict out);

SPPC_API int so_recv(int socket_fd, char *buffer, size_t size, int flags, ssize_t *restrict out);

SPPC_API int so_sendto(int socket_fd, char const *data, size_t size, so_addr_t const *restrict addr, ssize_t *restrict out);

SPPC_API int so_recvfrom(int socket_fd, char *buffer, size_t size, so_addr_t *restrict addr, ssize_t *restrict out);

SPPC_API int so_get_error(int socket_fd, int *restrict out);

SPPC_API int so_set_nonblocking(int socket_fd, bool non_blocking);

SPPC_API int so_set_recv_timeout(int socket_fd, int timeout_ms);

SPPC_API int so_set_send_timeout(int socket_fd, int timeout_ms);

SPPC_API int so_set_reuseaddr(int socket_fd, bool reuse);

SPPC_API int so_set_keepalive(int socket_fd, bool keepalive);

SPPC_API int so_set_linger(int socket_fd, bool linger);

SPPC_API int so_set_nodelay(int socket_fd, bool nodelay);

SPPC_API int so_getsockname(int socket_fd, so_addr_t *restrict addr);

SPPC_API int so_getpeername(int socket_fd, so_addr_t *restrict addr);

SPPC_API int ti_gettime(clockid_t clock, ti_duration_t *restrict out);

SPPC_API int ti_getres(clockid_t clock, ti_duration_t *restrict out);

SPPC_API int ti_nanosleep(clockid_t clock, ti_duration_t const *duration);

SPPC_API int ti_localtime(const ti_duration_t *restrict ts, ti_breakdown_t *restrict out);

SPPC_API int ti_mktime(const ti_breakdown_t *restrict bd, ti_duration_t *restrict out);

// SPPC_API int ti_format();

SPPC_API int ti_local_tz_name(char *restrict out);

SPPC_API pid_t pr_pid();

SPPC_API pid_t pr_ppid();

SPPC_API uid_t pr_get_uid();

SPPC_API int pr_set_uid(uid_t uid);

SPPC_API gid_t pr_get_gid();

SPPC_API int pr_set_gid(gid_t gid);

SPPC_API uid_t pr_get_euid();

SPPC_API gid_t pr_get_egid();

SPPC_API int pr_getenv(char const *restrict key, char *restrict out);

SPPC_API int pr_setenv(char const *restrict key, char const *restrict val, bool overwrite);

SPPC_API int pr_unsetenv(char const *restrict key);

// SPPC_API int pr_envvars(pr_env_t *restrict env);
//
// SPPC_API int pr_free_envvars(pr_env_t *env);

SPPC_API pid_t pr_exec(char const *restrict path, char const *const *restrict argv, char const *const *restrict envp, int stdin_fd, int stdout_fd, int stderr_fd);

SPPC_API int pr_waitpid(pid_t pid, pr_wait_result_t *restrict result);

SPPC_API int pr_waitpid_nowait(pid_t pid, pr_wait_result_t *restrict result);

SPPC_API int pr_signal(pid_t pid, int signal);

SPPC_API int pr_is_running(pid_t pid);

SPPC_API int pr_get_cwd(char *restrict out);

SPPC_API int pr_set_cwd(char const *restrict path);

SPPC_API void pr_exit(int status);

SPPC_API void pr_exit_clean(int status);

SPPC_API void pr_abort();

SPPC_API int rn_csprng_bytes(void *restrict out, size_t size);

SPPC_API int rn_csprng_u32(uint32_t *restrict out);

SPPC_API int rn_csprng_u64(uint64_t *restrict out);

SPPC_API int rn_csprng_range(uint64_t min, uint64_t max, uint64_t *restrict out);

SPPC_API int rn_prng_seed(uint64_t seed);

SPPC_API int rn_prng_reset(void);

SPPC_API int rn_prng_bytes(void *restrict out, size_t size);

SPPC_API int rn_prng_u32(uint32_t *restrict out);

SPPC_API int rn_prng_u64(uint64_t *restrict out);

SPPC_API int rn_prng_double(double *restrict out);

SPPC_API int rn_prng_next_range(uint64_t min, uint64_t max, uint64_t *restrict out);

// SPPC_API int rn_prng_jump(void);

SPPC_API int sys_sysconf(int what, int64_t *restrict out);

SPPC_API int sys_gettid(uint64_t *restrict out);

SPPC_API int sys_info(sys_info_t *restrict out);

// SPPC_API int sys_loadavg(sys_loadavg_t *restrict out);
//
// SPPC_API int sys_uname(sys_uname_t *restrict out);

SPPC_API int sys_gethostname(char *restrict out);

SPPC_API int sys_sethostname(char const *restrict name);

// SPPC_API int sys_getrlimit(int resource, sys_rlimit_t *restrict out);
//
// SPPC_API int sys_setrlimit(int resource, sys_rlimit_t const *restrict limit);
//
// SPPC_API int sys_cache_line_size(int64_t *restrict out);

SPPC_API int pt_thread_spawn(void*(*start_routine)(), uint64_t *restrict out);

SPPC_API int pt_thread_join(uint64_t handle);

SPPC_API int pt_thread_detach(uint64_t handle);

SPPC_API int pt_thread_signal(uint64_t handle, int sig);

SPPC_API int pt_thread_cancel(uint64_t handle);

SPPC_API int pt_thread_self(uint64_t *restrict out);

SPPC_API int pt_thread_equal(uint64_t handle1, uint64_t handle2);

SPPC_API int pt_thread_yield();

SPPC_API int pt_mutex_create(bool recursive, uint64_t *restrict out);

SPPC_API int pt_mutex_lock(uint64_t handle);

SPPC_API int pt_mutex_try_lock(uint64_t handle);

SPPC_API int pt_mutex_timeout_lock(uint64_t handle, ti_duration_t const *timeout);

SPPC_API int pt_mutex_unlock(uint64_t handle);

SPPC_API int pt_mutex_destroy(uint64_t handle);

SPPC_API int pt_condvar_create(uint64_t *restrict out);

SPPC_API int pt_condvar_wait(uint64_t handle, uint64_t mutex);

SPPC_API int pt_condvar_wait_timeout(uint64_t handle, uint64_t mutex, ti_duration_t const *timeout);

SPPC_API int pt_condvar_signal(uint64_t handle);

SPPC_API int pt_condvar_broadcast(uint64_t handle);

SPPC_API int pt_condvar_destroy(uint64_t handle);

SPPC_API int pt_rwlock_create(uint64_t *restrict out);

SPPC_API int pt_rwlock_read_lock(uint64_t handle);

SPPC_API int pt_rwlock_write_lock(uint64_t handle);

SPPC_API int pt_rwlock_try_read_lock(uint64_t handle);

SPPC_API int pt_rwlock_try_write_lock(uint64_t handle);

SPPC_API int pt_rwlock_read_timeout_lock(uint64_t handle, ti_duration_t const *timeout);

SPPC_API int pt_rwlock_write_timeout_lock(uint64_t handle, ti_duration_t const *timeout);

SPPC_API int pt_rwlock_unlock(uint64_t handle);

SPPC_API int pt_rwlock_destroy(uint64_t handle);

SPPC_API int pt_barrier_create(uint32_t count, uint64_t *restrict out);

SPPC_API int pt_barrier_wait(uint64_t handle);

SPPC_API int pt_barrier_destroy(uint64_t handle);

SPPC_API int pt_spinlock_create(uint64_t *restrict out);

SPPC_API int pt_spinlock_lock(uint64_t handle);

SPPC_API int pt_spinlock_try_lock(uint64_t spinlock_id);

SPPC_API int pt_spinlock_unlock(uint64_t spinlock_id);

SPPC_API int pt_spinlock_destroy(uint64_t spinlock_id);
