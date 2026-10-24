#include <sppc/sppc.h>

pthread_mutex_t _stdin_mutex;
pthread_mutex_t _stdout_mutex;
_sppc_thread_local sppc_closure _sppc_once_closure;
_sppc_thread_local int _sppc_once_ran;
pthread_mutex_t _stderr_mutex;

_sppc_export _sppc_tls_initial_exec
_sppc_thread_local void *__safestack_unsafe_stack_ptr = NULL;

_sppc_tls_initial_exec _sppc_thread_local void *_unsafe_stack_base = NULL;
_sppc_tls_initial_exec _sppc_thread_local size_t _unsafe_stack_size = 0;
bool _unsafe_stack_wanted = false;

_sppc_tls_initial_exec _sppc_thread_local gt_runtime *_gt_rt = NULL;

// Crash reporting state (backtrace.h).
#if SPPC_KERNEL_LINUX
_sppc_bt_object _sppc_bt_objects[_SPPC_BT_MAX_OBJECTS];
size_t _sppc_bt_nobjects = 0;
void const *_sppc_bt_exe_base = NULL;
#endif
bool _sppc_bt_enabled = true;
char const *_sppc_bt_off_note = "note: backtraces are off because SPP_BACKTRACE=0\n";
_sppc_atomic_flag _sppc_bt_busy = _SPPC_ATOMIC_FLAG_INIT;
_sppc_thread_local void *_sppc_bt_altstack = NULL;
_sppc_thread_local size_t _sppc_bt_altstack_size = 0;
char _sppc_bt_buf[_SPPC_BT_BUF_SIZE];
size_t _sppc_bt_len = 0;
pthread_key_t _gt_rt_key;
pthread_once_t _gt_rt_key_once = PTHREAD_ONCE_INIT;

// The trampoline's address is taken when a task's stack is laid out, so it
// needs a real out-of-line copy rather than being inlined at every use.
extern void _gt_trampoline(void);

// Variadic and noreturn, so it is never actually inlined; without this the
// only definition is the inline one and every use is an undefined reference.
extern void _gt_panic(const char *msg, ...);

extern gt_handle_t _gt_handle(gt_task const *t);
extern size_t _gt_page(void);
extern void* _gt_stack_alloc(size_t size);
extern void _gt_stack_unmap(void *usable, size_t size);
extern void _gt_stack_free(void *usable, size_t size);
extern void* _gt_stack_prime(void *usable, size_t size, void (*entry)(void));
extern void _gt_set_guard_pages(int enabled);
extern void _gt_rq_push(gt_task *t);
extern gt_task* _gt_rq_pop(void);
extern void _gt_reap(void);
extern void _gt_switch_to(gt_task *next);
extern gt_task* _gt_pick(void);
extern void _gt_block(int state);
extern gt_task* _gt_slot_alloc(void);
extern void _gt_slot_free(gt_task *t);
extern gt_task* _gt_resolve(gt_handle_t handle);
extern void _gt_init(void);
extern void _gt_init_slow(void);
extern void _gt_rt_free(void *rt);
extern void _gt_teardown(void);

// The crash reporter: its handler's address is taken, and the rest is called
// from it, so each needs one real out-of-line copy.
extern void _sppc_bt_put_n(char const *s, size_t n);
extern void _sppc_bt_put(char const *s);
extern void _sppc_bt_put_symbol(char const *name);
extern void _sppc_bt_flush(void);
extern void _sppc_bt_put_hex(uintptr_t v);
extern void _sppc_bt_put_dec(uint64_t v);
extern char const* _sppc_bt_signal_name(int sig);
#if SPPC_KERNEL_LINUX
extern void _sppc_bt_load(_sppc_bt_object *obj, char const *path);
extern _sppc_bt_object* _sppc_bt_object_for(Dl_info const *info);
extern uintptr_t _sppc_bt_sym_code(_sppc_bt_object const *obj, ElfW(Sym) const *s);
#elif SPPC_KERNEL_DARWIN
extern bool _sppc_bt_image_for(struct mach_header_64 const *mh, uintptr_t pc, _sppc_bt_image *out);
extern uintptr_t _sppc_bt_strip(uintptr_t addr);
extern bool _sppc_bt_read_frame(uintptr_t fp, uintptr_t out[2]);
#endif
extern bool _sppc_bt_is_cfi_trap(void const *pc);
extern char const* _sppc_bt_resolve(void const *lookup_pc, uintptr_t *out_start, char const **out_object);
extern bool _sppc_bt_secure(void);
extern void _sppc_bt_prepare(void);
extern size_t _sppc_bt_frames(void **frames, size_t max, void const *uctx);
extern void const* _sppc_bt_context_pc(void const *uctx);
extern void _sppc_bt_print(void const *uctx, void const *first_pc);
extern void _sppc_bt_handler(int sig, siginfo_t *info, void *uctx);
extern void _sppc_bt_fail_begin(void);
extern void _sppc_bt_cfi_failed(void const *uctx, void const *pc);
extern void _sppc_bt_fail_end(void const *caller_pc);
extern void _sppc_bt_altstack_up(void);
extern void _sppc_bt_altstack_down(void);
extern void _sppc_bt_install(void);
extern void _gt_rt_key_create(void);
extern void _gt_set_stack_size(size_t bytes);
extern int _gt_spawn(gt_handle_t *out, sppc_closure body, void (*entry)(void));
extern void _gt_yield(void);
extern int _gt_await(gt_handle_t handle);
extern int _gt_detach(gt_handle_t handle);
extern void _gt_drain(void);

extern int _sppc_fd_set_flags(int fd, bool cloexec, bool nonblock);
#if SPPC_KERNEL_DARWIN
extern int _sppc_backoff(clockid_t clock, struct timespec const *deadline, long *pause_ns);
extern int _sppc_pthread_mutex_clocklock(pthread_mutex_t *mutex, clockid_t clock, struct timespec const *deadline);
extern int _sppc_pthread_rwlock_clockrdlock(pthread_rwlock_t *rwlock, clockid_t clock, struct timespec const *deadline);
extern int _sppc_pthread_rwlock_clockwrlock(pthread_rwlock_t *rwlock, clockid_t clock, struct timespec const *deadline);
#endif

extern void* _sppc_thread_entry(void *closure);
extern void _sppc_once_entry(void);

extern int sppc_async(int *handle, sppc_closure body);
extern int sppc_await(int handle);
extern int sppc_async_detach(int handle);
extern void sppc_async_yield(void);
extern void sppc_async_stack_size(size_t bytes);
extern int sppc_unsafe_stack_up(void);
extern void sppc_unsafe_stack_down(void);
extern int sppc_init(void);
extern int sppc_deny_wx(void);
#if SPPC_KERNEL_LINUX
extern int _sppc_seal_range(uintptr_t start, uintptr_t end, uintptr_t page);
extern int _sppc_seal_object(struct dl_phdr_info *info, size_t size, void *data);
#endif
extern int sppc_seal_segments(void);
extern int sppc_cleanup(void);
extern int sppc_pthread_create(sppc_closure start_routine, uint64_t *_sppc_restrict out);
extern int sppc_pthread_join(uint64_t const *_sppc_restrict handle);
extern int sppc_pthread_detach(uint64_t const *_sppc_restrict handle);
extern void sppc_pthread_equal(uint64_t const *handle1, uint64_t const *handle2, uint64_t *_sppc_restrict out);
extern void sppc_pthread_self(uint64_t *_sppc_restrict out);
extern int sppc_pthread_mutex_init(uint64_t *_sppc_restrict out);
extern int sppc_pthread_mutex_init_recursive(uint64_t *_sppc_restrict out);
extern int sppc_pthread_mutex_lock(uint64_t const *_sppc_restrict mutex);
extern int sppc_pthread_mutex_trylock(uint64_t const *_sppc_restrict mutex);
extern int sppc_pthread_mutex_unlock(uint64_t const *_sppc_restrict mutex);
extern int sppc_pthread_mutex_destroy(uint64_t const *_sppc_restrict mutex);
extern int sppc_pthread_once_init(uint64_t *_sppc_restrict out);
extern int sppc_pthread_once(uint64_t const *_sppc_restrict once, sppc_closure func);
extern int sppc_pthread_cond_init(uint64_t *_sppc_restrict out);
extern int sppc_pthread_cond_wait(uint64_t const *_sppc_restrict cond, uint64_t const *_sppc_restrict mutex);
extern int sppc_pthread_cond_signal(uint64_t const *_sppc_restrict cond);
extern int sppc_pthread_cond_broadcast(uint64_t const *_sppc_restrict cond);
extern int sppc_pthread_cond_destroy(uint64_t const *_sppc_restrict cond);
extern int sppc_pthread_rwlock_init(uint64_t *_sppc_restrict rwlock);
extern int sppc_pthread_rwlock_rdlock(uint64_t const *_sppc_restrict rwlock);
extern int sppc_pthread_rwlock_tryrdlock(uint64_t const *_sppc_restrict rwlock);
extern int sppc_pthread_rwlock_wrlock(uint64_t const *_sppc_restrict rwlock);
extern int sppc_pthread_rwlock_trywrlock(uint64_t const *_sppc_restrict rwlock);
extern int sppc_pthread_rwlock_unlock(uint64_t const *_sppc_restrict rwlock);
extern int sppc_pthread_rwlock_destroy(uint64_t const *_sppc_restrict rwlock);
extern void* sppc_malloc(size_t size);
extern void* sppc_aligned_alloc(size_t size, size_t alignment);
extern void* sppc_calloc(size_t num, size_t size);
extern void* sppc_realloc(void *ptr, size_t new_size);
extern void sppc_free(void *ptr);
extern int sppc_memcpy(void *_sppc_restrict dest, void const *_sppc_restrict src, size_t size, size_t dest_index, size_t src_index);
extern int sppc_memmove(void *dest, void const *src, size_t size);
extern int sppc_memset(void *_sppc_restrict dest, int value, size_t size, size_t dest_index);
extern int sppc_memcmp(void const *ptr1, void const *ptr2, size_t size, int *_sppc_restrict out);
extern int sppc_memmem(void const *haystack, size_t haystack_size, void const *needle, size_t needle_size, size_t *_sppc_restrict out);
extern int sppc_read(char *_sppc_restrict buffer, size_t size, size_t count, int fd, ssize_t *_sppc_restrict out_n);
extern int sppc_write(char const *_sppc_restrict buffer, size_t size, size_t count, int fd, ssize_t *_sppc_restrict out_n);
extern int sppc_open(char const *_sppc_restrict path, int flags, uint32_t mode, int *_sppc_restrict out_fd);
extern int sppc_close(int fd);
extern int sppc_stat(char const *_sppc_restrict path, struct sppc_stat *_sppc_restrict out);
extern int sppc_fstat(int fd, struct sppc_stat *_sppc_restrict out);
extern int sppc_lstat(char const *_sppc_restrict path, struct sppc_stat *_sppc_restrict out);
extern int sppc_poll(sppc_pollfd *_sppc_restrict fds, uint64_t count, int timeout, int *_sppc_restrict out_n);
extern int sppc_lseek(int fd, int64_t offset, int whence, int64_t *_sppc_restrict out_pos);
extern void* sppc_mmap(int fd, size_t length, int prot, int flags, int64_t offset);
extern int sppc_memprotect(void *addr, size_t size, int prot);
extern int sppc_munmap(void *_sppc_restrict addr, size_t const *_sppc_restrict length);
extern int sppc_pread(int fd, void *_sppc_restrict buffer, size_t size, size_t count, int64_t offset, ssize_t *_sppc_restrict out_n);
extern int sppc_pwrite(int fd, void const *_sppc_restrict buffer, size_t size, size_t count, int64_t offset, ssize_t *_sppc_restrict out_n);
extern int sppc_readv(int fd, sppc_iovec const *_sppc_restrict iov, int iov_count, ssize_t *_sppc_restrict out_n);
extern int sppc_writev(int fd, sppc_iovec const *_sppc_restrict iov, int iov_count, ssize_t *_sppc_restrict out_n);
extern int sppc_access(char const *_sppc_restrict path, int flags, int *_sppc_restrict out);
extern int sppc_select(int nfds, sppc_fd_set *_sppc_restrict readfds, sppc_fd_set *_sppc_restrict writefds, sppc_fd_set *_sppc_restrict exceptfds, sppc_timeval *_sppc_restrict timeout, int *_sppc_restrict out_n);
extern void sppc_sched_yield(void);
extern int sppc_msync(void *_sppc_restrict addr, size_t const *_sppc_restrict length, int flags);
extern int sppc_madvise(void *_sppc_restrict addr, size_t const *_sppc_restrict length, int advice);
extern int sppc_dup(int fd, int *_sppc_restrict out_fd);
extern int sppc_dup2(int fd, int target_fd);
extern void sppc_get_pid(int32_t *_sppc_restrict out_pid);
extern int sppc_socket(int domain, int type, int protocol, int *_sppc_restrict out_fd);
extern int sppc_connect(int fd, sppc_sockaddr_storage const *_sppc_restrict storage);
extern int sppc_sendto(int fd, char const *data, size_t size, sppc_sockaddr_storage const *_sppc_restrict storage, ssize_t *_sppc_restrict out_n);
extern int sppc_recvfrom(int fd, char *buffer, size_t size, sppc_sockaddr_storage *_sppc_restrict out_storage, ssize_t *_sppc_restrict out_n);
extern int sppc_send(int fd, char const *data, size_t size, int flags, ssize_t *_sppc_restrict out_n);
extern int sppc_recv(int fd, char *buffer, size_t size, int flags, ssize_t *_sppc_restrict out_n);
extern int sppc_shutdown(int fd, int how);
extern int sppc_bind(int fd, sppc_sockaddr_storage const *_sppc_restrict storage);
extern int sppc_listen(int fd, int backlog);
extern int sppc_getsockname(int fd, sppc_sockaddr_storage *_sppc_restrict out_storage);
extern int sppc_getpeername(int fd, sppc_sockaddr_storage *_sppc_restrict out_storage);
extern void sppc_exit(int status);
extern int sppc_signal(int32_t pid, int signal);
extern int sppc_fcntl_get(int fd, int cmd, int *_sppc_restrict out);
extern int sppc_fcntl_set(int fd, int cmd, int arg);
extern int sppc_fcntl_lock(int fd, int cmd, sppc_flock *_sppc_restrict lock);
extern int sppc_fsync(int fd);
extern int sppc_truncate(char const *_sppc_restrict path, int64_t length);
extern int sppc_ftruncate(int fd, int64_t length);
extern int sppc_getcwd(char *_sppc_restrict buffer, size_t size);
extern int sppc_chdir(char const *_sppc_restrict path);
extern int sppc_rename(char const *_sppc_restrict old_path, char const *_sppc_restrict new_path);
extern int sppc_mkdir(char const *_sppc_restrict path, uint32_t mode);
extern int sppc_rmdir(char const *_sppc_restrict path);
extern int sppc_link(char const *_sppc_restrict target, char const *_sppc_restrict linkpath);
extern int sppc_symlink(char const *_sppc_restrict target, char const *_sppc_restrict linkpath);
extern int sppc_unlink(char const *_sppc_restrict path);
extern int sppc_readlink(char const *_sppc_restrict path, char *_sppc_restrict buffer, size_t buffer_size);
extern int sppc_chmod(char const *_sppc_restrict path, uint32_t mode);
extern int sppc_chown(char const *_sppc_restrict path, uint32_t owner, uint32_t group);
extern int sppc_lchown(char const *_sppc_restrict path, uint32_t owner, uint32_t group);
extern void sppc_get_ppid(int32_t *_sppc_restrict out_ppid);
extern int sppc_mlock(const void *addr, size_t size);
extern int sppc_munlock(const void *addr, size_t size);
extern int sppc_clock_gettime(int clock_id, sppc_timespec *_sppc_restrict out_tp);
extern int sppc_utimensat(char const *_sppc_restrict path, int flags);
extern int sppc_realpath(char const *_sppc_restrict path, char *_sppc_restrict buffer);
extern int sppc_mktemp(char *_sppc_restrict path, int *_sppc_restrict out_fd);
extern int sppc_mktemp_dir(char *_sppc_restrict path);
extern int sppc_statvfs(char const *_sppc_restrict path, struct sppc_statvfs *_sppc_restrict out);
extern int sppc_strcpy(char *_sppc_restrict dest, char const *_sppc_restrict src);
extern int sppc_strcat(char *_sppc_restrict dest, char const *_sppc_restrict src);
extern int sppc_strcmp(char const *str1, char const *str2, bool *_sppc_restrict out);
extern int sppc_strcasecmp(char const *str1, char const *str2, bool *_sppc_restrict out);
extern int sppc_strchr(char const *str, char ch, size_t *_sppc_restrict out_idx);
extern int sppc_strrchr(char const *str, char ch, size_t *_sppc_restrict out_idx);
extern int sppc_strstr(char const *haystack, char const *needle, size_t *_sppc_restrict out_idx);
extern int sppc_strrstr(char const *haystack, char const *needle, size_t *_sppc_restrict out_idx);
extern int sppc_strcasestr(char const *haystack, char const *needle, size_t *_sppc_restrict out_idx);
extern int sppc_strpbrk(char const *string, char const *accept, size_t *_sppc_restrict out_idx);
extern void* sppc_strdup(char const *str);
extern int sppc_getenv(char const *_sppc_restrict key, char *_sppc_restrict out, size_t size);
extern int sppc_setenv(char const *_sppc_restrict key, char const *_sppc_restrict val, bool overwrite);
extern int sppc_unsetenv(char const *_sppc_restrict key);
extern void sppc_halt(int status);
extern void sppc_abort(void);
extern void sppc_check_failed(uint32_t check, uint64_t a, uint64_t b);
extern void sppc_set_sockaddr_v4(uint8_t const *octets, uint16_t port, sppc_sockaddr_storage *_sppc_restrict out_storage);
extern void sppc_set_sockaddr_v6(uint16_t const *segments, uint16_t port, sppc_sockaddr_storage *_sppc_restrict out_storage);
extern void sppc_get_sockaddr_v4(sppc_sockaddr_storage const *_sppc_restrict storage, uint8_t *out_octets, uint16_t *out_port);
extern void sppc_get_sockaddr_v6(sppc_sockaddr_storage const *_sppc_restrict storage, uint16_t *out_segments, uint16_t *out_port);
extern void sppc_sockaddr_family(sppc_sockaddr_storage const *_sppc_restrict storage, int *_sppc_restrict out_family);
extern int sppc_setsockopt(int fd, int level, int optname, int const *_sppc_restrict optval);
extern int sppc_getsockopt(int fd, int level, int optname, int *_sppc_restrict optval);
extern int sppc_setsockopt_timeval(int fd, int level, int optname, sppc_timeval const *_sppc_restrict optval);
extern int sppc_getsockopt_timeval(int fd, int level, int optname, sppc_timeval *_sppc_restrict optval);
extern int sppc_setsockopt_linger(int fd, int level, int optname, sppc_linger const *_sppc_restrict optval);
extern int sppc_getsockopt_linger(int fd, int level, int optname, sppc_linger *_sppc_restrict optval);
extern int sppc_stdin_read(char *_sppc_restrict buffer, size_t size, size_t count, ssize_t *_sppc_restrict out_n);
extern int sppc_stdout_write(char const *_sppc_restrict buffer, size_t size, size_t count, ssize_t *_sppc_restrict out_n);
extern int sppc_stderr_write(char const *_sppc_restrict buffer, size_t size, size_t count, ssize_t *_sppc_restrict out_n);
extern int sppc_pthread_mutex_clocklock(uint64_t const *_sppc_restrict mutex, int clock, sppc_timespec const *_sppc_restrict duration);
extern int sppc_pthread_cond_clockwait(uint64_t const *_sppc_restrict cond, uint64_t const *_sppc_restrict mutex, int clock, sppc_timespec const *_sppc_restrict duration);
extern int sppc_pthread_rwlock_clockrdlock(uint64_t const *_sppc_restrict rwlock, int clock, sppc_timespec const *_sppc_restrict duration);
extern int sppc_pthread_rwlock_clockwrlock(uint64_t const *_sppc_restrict rwlock, int clock, sppc_timespec const *_sppc_restrict duration);
extern int sppc_pthread_barrier_init(uint64_t *_sppc_restrict barrier, uint64_t count);
extern int sppc_pthread_barrier_wait(uint64_t const *_sppc_restrict barrier);
extern int sppc_pthread_barrier_destroy(uint64_t const *_sppc_restrict barrier);
extern int sppc_pthread_spin_init(uint64_t *_sppc_restrict spinlock);
extern int sppc_pthread_spin_lock(uint64_t const *_sppc_restrict spinlock);
extern int sppc_pthread_spin_trylock(uint64_t const *_sppc_restrict spinlock);
extern int sppc_pthread_spin_unlock(uint64_t const *_sppc_restrict spinlock);
extern int sppc_pthread_spin_destroy(uint64_t const *_sppc_restrict spinlock);
extern int sppc_pipe(int *_sppc_restrict out_read_fd, int *_sppc_restrict out_write_fd);
extern int sppc_sendfile(int from_fd, int to_fd, int64_t *offset, size_t count, ssize_t *_sppc_restrict out_n);
extern int sppc_accept(int fd, sppc_sockaddr_storage *_sppc_restrict out_storage, int *_sppc_restrict out_fd);
extern int sppc_fdatasync(int fd);
extern int sppc_clock_nanosleep(int clock, int flags, sppc_timespec const *_sppc_restrict duration);
extern int sppc_getrandom(size_t size, char *_sppc_restrict out);
extern int sppc_copyfile(int fd_in, int fd_out, size_t len, int flags, ssize_t *_sppc_restrict out_n);
