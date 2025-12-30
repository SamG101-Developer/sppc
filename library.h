#pragma once

#include <macros.h>
#include <stddef.h>


/**
 * https://en.cppreference.com/w/c/io/fread
 * A low level function to read data from a stream to a buffer. The total size of data read is determined by multiplying
 * the size of each element by the number of elements to read. The stream is specified by its file descriptor.
 * @param[out] buffer The buffer to read data into. This is the mutable "sret" style parameter.
 * @param[in] size The size of each element to read, in bytes.
 * @param[in] count The number of elements to read.
 * @param[in] fd The file descriptor of the input stream to read data from.
 * @return A size_t value representing the total number of elements successfully read from the stream.
 */
SPPC_API size_t fd_read(void *restrict buffer, size_t size, size_t count, int fd);


/**
 * https://en.cppreference.com/w/c/io/fwrite
 * A low level function to write data from a buffer to a stream. The total size of data written is determined by
 * multiplying the size of each element by the number of elements to write. The stream is specified by its file
 * descriptor.
 * @param buffer The buffer to write data from. This is the immutable "byval" style parameter.
 * @param size The size of each element to write, in bytes.
 * @param count The number of elements to write.
 * @param fd The file descriptor of the output stream to write data to.
 * @return A size_t value representing the total number of elements successfully written to the stream.
 */
SPPC_API size_t fd_write(void const *restrict buffer, size_t size, size_t count, int fd);


/**
 * https://en.cppreference.com/w/c/io/fopen
 * A low level function to open a file and return its file descriptor. The file is specified by its name and the mode
 * in which to open it (e.g., read, write, append).
 * @param filename The name of the file to open.
 * @param mode The mode in which to open the file (e.g., "r" for read, "w" for write).
 * @return The file descriptor as a signed 32-bit integer. Returns a negative value on failure.
 */
SPPC_API int fd_open(unsigned char const *restrict filename, unsigned char const *restrict mode);

SPPC_API int fd_close(int fd);

SPPC_API int fd_flush(int fd);

SPPC_API int fd_seek(int fd, long long offset, int whence);

SPPC_API long long fd_tell(int fd);

SPPC_API int fd_truncate(int fd, long long length);

SPPC_API int fd_lock_ex(int fd, bool non_blocking);

SPPC_API int fd_lock_sh(int fd, bool non_blocking);

SPPC_API int fd_unlock(int fd);

SPPC_API void* mem_alloc(size_t size);

SPPC_API void* mem_calloc(size_t num, size_t size);

SPPC_API void mem_dealloc(void *ptr);

SPPC_API void* mem_realloc(void *ptr, size_t new_size);

SPPC_API int net_sock_init(int domain, int type, int protocol);

SPPC_API long long net_sock_send(int socket_fd, unsigned char const *data, size_t size);

SPPC_API long long net_sock_recv(int socket_fd, unsigned char *buffer, size_t size);

SPPC_API int net_sock_conn(int socket_fd, unsigned char const *restrict host, unsigned short port);

SPPC_API long long net_sock_bind(int socket_fd, unsigned char const *restrict host, unsigned short port);

SPPC_API int net_sock_listen(int socket_fd, int backlog);

SPPC_API int net_sock_accept(int socket_fd);

SPPC_API int net_sock_close(int socket_fd);

SPPC_API long long net_sock_sendto(int socket_fd, unsigned char const *data, size_t size, unsigned char const *restrict host, unsigned short port);

SPPC_API long long net_sock_recvfrom(int socket_fd, unsigned char *buffer, size_t size);

SPPC_API int net_sock_set_nonblocking(int socket_fd, bool non_blocking);

SPPC_API int net_sock_set_recv_timeout(int socket_fd, int timeout_ms);

SPPC_API int net_sock_set_send_timeout(int socket_fd, int timeout_ms);

SPPC_API int net_sock_set_reuseaddr(int socket_fd, bool reuse);

SPPC_API int net_sock_set_keepalive(int socket_fd, bool keepalive);

SPPC_API int net_sock_set_nodelay(int socket_fd, bool nodelay);

SPPC_API long long time_now_ns(int clock);

SPPC_API void time_sleep_ms(long long milliseconds);

SPPC_API void time_sleep_us(long long microseconds);

SPPC_API void time_sleep_ns(long long nanoseconds);

SPPC_API long long time_local_tz_offset_seconds();

SPPC_API int time_local_tz_name(unsigned char *restrict buffer, size_t size);

SPPC_API int proc_get_pid();

SPPC_API int proc_get_parent_pid();

SPPC_API int proc_set_env(unsigned char const *restrict key, unsigned char const *restrict value, bool overwrite);

SPPC_API unsigned char* proc_get_env(unsigned char const *restrict key);

SPPC_API int proc_unset_env(unsigned char const *restrict key);

SPPC_API int proc_exec(unsigned char const *restrict path, unsigned char const *const *restrict argv, unsigned char const *const *restrict envp);

SPPC_API int proc_wait(int pid);

SPPC_API int proc_kill(int pid);

SPPC_API int proc_is_running(int pid);

SPPC_API unsigned char* proc_getcwd(unsigned char *restrict buffer, size_t size);

SPPC_API int proc_setcwd(unsigned char const *restrict path);

SPPC_API int proc_pipe(int read_fd, int write_fd);

SPPC_API void rng_seed(unsigned long long seed);

SPPC_API unsigned long long rng_next();

SPPC_API double rng_next_double();

SPPC_API void rng_fill_bytes(unsigned char *buffer, size_t size);

SPPC_API int fs_exists(unsigned char const *restrict path);

SPPC_API int fs_isfile(unsigned char const *restrict path);

SPPC_API int fs_isdir(unsigned char const *restrict path);

SPPC_API int fs_issymlink(unsigned char const *restrict path);

SPPC_API long long fs_file_size(unsigned char const *restrict path);

SPPC_API int fs_remove(unsigned char const *restrict path);

SPPC_API int fs_rename(unsigned char const *restrict old_path, unsigned char const *restrict new_path);

SPPC_API int fs_mkdir(unsigned char const *restrict path, unsigned int mode);

SPPC_API int fs_rmdir(unsigned char const *restrict path);

SPPC_API int fs_chmod(unsigned char const *restrict path, unsigned int mode);

SPPC_API int fs_symlink_target(unsigned char const *restrict path, unsigned char *restrict buffer, size_t size);

SPPC_API int sys_cpu_count();

SPPC_API long long sys_page_size();

SPPC_API unsigned long long sys_thread_id();

SPPC_API int thread_spawn(void*(*start_routine)(void *));

SPPC_API int thread_join(int thread_id);

SPPC_API int thread_detach(int thread_id);

SPPC_API int thread_kill(int thread_id);

SPPC_API int thread_cancel(int thread_id);

SPPC_API int thread_equal(int thread_id_1, int thread_id_2);

SPPC_API int thread_yield();

SPPC_API int thread_sleep_ms(long long milliseconds);

SPPC_API int thread_get_id();

SPPC_API int mutex_create();

SPPC_API int mutex_lock(int mutex_id);

SPPC_API int mutex_trylock(int mutex_id);

SPPC_API int mutex_unlock(int mutex_id);

SPPC_API int mutex_destroy(int mutex_id);

SPPC_API int condvar_create();

SPPC_API int condvar_wait(int condvar_id, int mutex_id);

SPPC_API int condvar_signal(int condvar_id);

SPPC_API int condvar_broadcast(int condvar_id);

SPPC_API int condvar_destroy(int condvar_id);

SPPC_API int rwlock_create();

SPPC_API int rwlock_rdlock(int rwlock_id);

SPPC_API int rwlock_wrlock(int rwlock_id);

SPPC_API int rwlock_tryrdlock(int rwlock_id);

SPPC_API int rwlock_trywrlock(int rwlock_id);

SPPC_API int rwlock_unlock(int rwlock_id);

SPPC_API int rwlock_destroy(int rwlock_id);

SPPC_API int barrier_create(int count);

SPPC_API int barrier_wait(int barrier_id);

SPPC_API int barrier_destroy(int barrier_id);

SPPC_API int spinlock_create();

SPPC_API int spinlock_lock(int spinlock_id);

SPPC_API int spinlock_trylock(int spinlock_id);

SPPC_API int spinlock_unlock(int spinlock_id);

SPPC_API int spinlock_destroy(int spinlock_id);
