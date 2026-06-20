#define _GNU_SOURCE

#include <sppc2/sppc2.h>
#include <sppc2/inner/rng.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <malloc.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/random.h>
#include <sys/socket.h>

static pthread_mutex_t _stdin_mutex;
static pthread_mutex_t _stdout_mutex;
static pthread_mutex_t _stderr_mutex;

#define _return_normalized_err \
    return err < 0 ? errno : 0;

#define _return_normalized_void_err \
    return err == NULL ? errno : 0;

#define _return_normalized_pthread_err \
    return err != 0 ? errno : 0;

#define _return_normalized_ptr_err \
    return err == NULL ? errno : 0;

#define _return_special_error(errno_val, return_val) \
    if (err == errno_val) { return return_val; }

#define _return_pointer \
    return err;

#define _return_success \
    return 0;

#define _extract_err \
    const auto err =

#define _socket_addr_in_construction_helper \
    socklen_t len = storage->ss_family == AF_INET ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);

#define _socket_addr_out_construction_helper \
    socklen_t len = out_storage->ss_family == AF_INET ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);

#define pthread_mutex_init_helper(flag)                                 \
    ({ pthread_mutexattr_t attr;                                        \
    pthread_mutexattr_init(&attr);                                      \
    pthread_mutexattr_settype(&attr, (flag));                           \
    const auto err_ = pthread_mutex_init((pthread_mutex_t*)out, &attr); \
    pthread_mutexattr_destroy(&attr);                                   \
    err_; })

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

int c_init(void) {
    signal(SIGPIPE, SIG_IGN); // let write() return EPIPE instead of killing the process when writing to a closed fd.
    signal(SIGCHLD, SIG_DFL); // ensure zombie reaping works correctly.
    signal(SIGHUP, SIG_IGN); // ignore SIGHUP to prevent accidental termination when the controlling terminal is closed.
    setlocale(LC_ALL, "en_GB.UTF-8");
    tzset();
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    mallopt(M_TRIM_THRESHOLD, 128 * 1024); // trim after 128KB free
    mallopt(M_MMAP_THRESHOLD, 64 * 1024); // mmap allocations above 64KB

    if (pthread_mutex_init(&_stdin_mutex, NULL) != 0) { return errno; }
    if (pthread_mutex_init(&_stdout_mutex, NULL) != 0) { return errno; }
    if (pthread_mutex_init(&_stderr_mutex, NULL) != 0) { return errno; }
    _return_success
}

int c_cleanup(void) {
    if (pthread_mutex_destroy(&_stdin_mutex) != 0) { return errno; }
    if (pthread_mutex_destroy(&_stdout_mutex) != 0) { return errno; }
    if (pthread_mutex_destroy(&_stderr_mutex) != 0) { return errno; }
    _return_success
}

int c_pthread_create(void const *start_routine, uint64_t *out) {
    _extract_err pthread_create(out, NULL, (void*)start_routine, NULL);
    _return_normalized_pthread_err
}

int c_pthread_join(uint64_t const *restrict handle) {
    _extract_err pthread_join((pthread_t)handle, nullptr);
    _return_normalized_pthread_err
}

int c_pthread_detach(uint64_t const *restrict handle) {
    _extract_err pthread_detach((pthread_t)handle);
    _return_normalized_pthread_err
}

int c_pthread_equal(uint64_t const *handle1, uint64_t const *handle2, uint64_t *restrict out) {
    *out = pthread_equal((pthread_t)handle1, (pthread_t)handle2) != 0;
    _return_success
}

int c_pthread_self(uint64_t *restrict out) {
    *out = pthread_self();
    _return_success
}

void c_sched_yield(void) {
    sched_yield();
}

int c_pthread_mutex_init(uint64_t *restrict out) {
    _extract_err pthread_mutex_init_helper(DEBUG_BUILD ? PTHREAD_MUTEX_ERRORCHECK : PTHREAD_MUTEX_NORMAL);
    _return_normalized_pthread_err
}

int c_pthread_mutex_init_recursive(uint64_t *restrict out) {
    _extract_err pthread_mutex_init_helper(PTHREAD_MUTEX_RECURSIVE);
    _return_normalized_pthread_err
}

int c_pthread_mutex_lock(uint64_t const *restrict mutex) {
    _extract_err pthread_mutex_lock((pthread_mutex_t*)mutex);
    _return_normalized_pthread_err
}

int c_pthread_mutex_timedlock(uint64_t const *restrict mutex, const clockid_t clock, struct timespec const *restrict duration) {
    _extract_err pthread_mutex_clocklock((pthread_mutex_t*)mutex, clock, duration);
    _return_special_error(ETIMEDOUT, 1)
    _return_normalized_pthread_err
}

int c_pthread_mutex_trylock(uint64_t const *restrict mutex) {
    _extract_err pthread_mutex_trylock((pthread_mutex_t*)mutex);
    _return_special_error(EBUSY, 1)
    _return_normalized_pthread_err
}

int c_pthread_unlock(uint64_t const *restrict mutex) {
    _extract_err pthread_mutex_unlock((pthread_mutex_t*)mutex);
    _return_normalized_pthread_err
}

int c_pthread_mutex_destroy(uint64_t const *restrict mutex) {
    _extract_err pthread_mutex_destroy((pthread_mutex_t*)mutex);
    _return_normalized_pthread_err
}

int c_pthread_once(void const *restrict func) {
    constexpr auto flag = PTHREAD_ONCE_INIT;
    _extract_err pthread_once((pthread_once_t*)&flag, (void(*)())func);
    _return_normalized_pthread_err
}

int c_pthread_cond_init(uint64_t *restrict out) {
    _extract_err pthread_cond_init((pthread_cond_t*)out, nullptr);
    _return_normalized_pthread_err
}

int c_pthread_cond_wait(uint64_t const *restrict cond, uint64_t const *restrict mutex) {
    _extract_err pthread_cond_wait((pthread_cond_t*)cond, (pthread_mutex_t*)mutex);
    _return_normalized_pthread_err
}

int c_pthread_cond_timedwait(uint64_t const *restrict cond, uint64_t const *restrict mutex, const clockid_t clock, struct timespec const *restrict duration) {
    _extract_err pthread_cond_clockwait((pthread_cond_t*)cond, (pthread_mutex_t*)mutex, clock, duration);
    _return_special_error(ETIMEDOUT, 1)
    _return_normalized_pthread_err
}

int c_pthread_cond_signal(uint64_t const *restrict cond) {
    _extract_err pthread_cond_signal((pthread_cond_t*)cond);
    _return_normalized_pthread_err
}

int c_pthread_cond_broadcast(uint64_t const *restrict cond) {
    _extract_err pthread_cond_broadcast((pthread_cond_t*)cond);
    _return_normalized_pthread_err
}

int c_pthread_cond_destroy(uint64_t const *restrict cond) {
    _extract_err pthread_cond_destroy((pthread_cond_t*)cond);
    _return_normalized_pthread_err
}

int c_pthread_rwlock_init(uint64_t *restrict rwlock) {
    _extract_err pthread_rwlock_init((pthread_rwlock_t*)rwlock, nullptr);
    _return_normalized_pthread_err
}

int c_pthread_rwlock_rdlock(uint64_t const *restrict rwlock) {
    _extract_err pthread_rwlock_rdlock((pthread_rwlock_t*)rwlock);
    _return_normalized_pthread_err
}

int c_pthread_rwlock_tryrdlock(uint64_t const *restrict rwlock) {
    _extract_err pthread_rwlock_tryrdlock((pthread_rwlock_t*)rwlock);
    _return_special_error(EBUSY, 1)
    _return_normalized_pthread_err
}

int c_pthread_rwlock_clockrdlock(uint64_t const *restrict rwlock, const clockid_t clock, struct timespec const *restrict duration) {
    _extract_err pthread_rwlock_clockrdlock((pthread_rwlock_t*)rwlock, clock, duration);
    _return_special_error(ETIMEDOUT, 1)
    _return_normalized_pthread_err
}

int c_pthread_rwlock_wrlock(uint64_t *restrict rwlock) {
    _extract_err pthread_rwlock_wrlock((pthread_rwlock_t*)rwlock);
    _return_normalized_pthread_err
}

int c_pthread_rwlock_trywrlock(uint64_t *restrict rwlock) {
    _extract_err pthread_rwlock_trywrlock((pthread_rwlock_t*)rwlock);
    _return_special_error(EBUSY, 1)
    _return_normalized_pthread_err
}

int c_pthread_rwlock_clockwrlock(uint64_t const *restrict rwlock, const clockid_t clock, struct timespec const *restrict duration) {
    _extract_err pthread_rwlock_clockwrlock((pthread_rwlock_t*)rwlock, clock, duration);
    _return_special_error(ETIMEDOUT, 1)
    _return_normalized_pthread_err
}

int c_pthread_rwlock_unlock(uint64_t const *restrict rwlock) {
    _extract_err pthread_rwlock_unlock((pthread_rwlock_t*)rwlock);
    _return_normalized_pthread_err
}

int c_pthread_rwlock_destroy(uint64_t const *restrict rwlock) {
    _extract_err pthread_rwlock_destroy((pthread_rwlock_t*)rwlock);
    _return_normalized_pthread_err
}

int c_pthread_barrier_init(uint64_t *restrict barrier, const uint64_t count) {
    _extract_err pthread_barrier_init((pthread_barrier_t*)barrier, nullptr, count);
    _return_normalized_pthread_err
}

int c_pthread_barrier_wait(uint64_t const *restrict barrier) {
    _extract_err pthread_barrier_wait((pthread_barrier_t*)barrier);
    _return_special_error(PTHREAD_BARRIER_SERIAL_THREAD, 1)
    _return_normalized_pthread_err
}

int c_pthread_barrier_destroy(uint64_t const *restrict barrier) {
    _extract_err pthread_barrier_destroy((pthread_barrier_t*)barrier);
    _return_normalized_pthread_err
}

int c_pthread_spin_init(uint64_t *restrict spinlock) {
    _extract_err pthread_spin_init((pthread_spinlock_t*)spinlock, 0);
    _return_normalized_pthread_err
}

int c_pthread_spin_lock(uint64_t const *restrict spinlock) {
    _extract_err pthread_spin_lock((pthread_spinlock_t*)spinlock);
    _return_normalized_pthread_err
}

int c_pthread_spin_trylock(uint64_t const *restrict spinlock) {
    _extract_err pthread_spin_trylock((pthread_spinlock_t*)spinlock);
    _return_special_error(EBUSY, 1)
    _return_normalized_pthread_err
}

int c_pthread_spin_unlock(uint64_t const *restrict spinlock) {
    _extract_err pthread_spin_unlock((pthread_spinlock_t*)spinlock);
    _return_normalized_pthread_err
}

int c_pthread_spin_destroy(uint64_t const *restrict spinlock) {
    _extract_err pthread_spin_destroy((pthread_spinlock_t*)spinlock);
    _return_normalized_pthread_err
}

int c_read(void *restrict buffer, const size_t size, const size_t count, const int fd, ssize_t *restrict out_n) {
    _extract_err read(fd, buffer, size * count);
    *out_n = err < 0 ? -1 : err;
    _return_normalized_err
}

int c_write(void const *restrict buffer, const size_t size, const size_t count, const int fd, ssize_t *restrict out_n) {
    _extract_err write(fd, buffer, size * count);
    *out_n = err < 0 ? -1 : err;
    _return_normalized_err
}

int c_stdin_read(void *restrict buffer, const size_t size, const size_t count, ssize_t *restrict out_n) {
    ssize_t err = 0;
    if (pthread_mutex_lock(&_stdin_mutex) != 0) { return errno; }
    if (c_read(buffer, size, count, STDIN_FILENO, &err) != 0) { return errno; }
    if (pthread_mutex_unlock(&_stdin_mutex) != 0) { return errno; }
    *out_n = err < 0 ? -1 : err;
    _return_normalized_err
}

int c_stdout_write(void const *restrict buffer, const size_t size, const size_t count, ssize_t *restrict out_n) {
    ssize_t err = 0;
    if (pthread_mutex_lock(&_stdout_mutex) != 0) { return errno; }
    if (c_write(buffer, size, count, STDOUT_FILENO, &err) != 0) { return errno; }
    if (pthread_mutex_unlock(&_stdout_mutex) != 0) { return errno; }
    *out_n = err < 0 ? -1 : err;
    _return_normalized_err
}

int c_stderr_write(void const *restrict buffer, const size_t size, const size_t count, ssize_t *restrict out_n) {
    ssize_t err = 0;
    if (pthread_mutex_lock(&_stderr_mutex) != 0) { return errno; }
    if (c_write(buffer, size, count, STDERR_FILENO, &err) != 0) { return errno; }
    if (pthread_mutex_unlock(&_stderr_mutex) != 0) { return errno; }
    *out_n = err < 0 ? -1 : err;
    _return_normalized_err
}

int c_open(char const *restrict path, const int flags, const mode_t mode, int *restrict out_fd) {
    _extract_err open(path, flags, mode);
    *out_fd = err < 0 ? -1 : err;
    _return_normalized_err
}

int c_close(const int fd) {
    _extract_err close(fd);
    _return_normalized_err
}

int c_flush(const int fd) {
    _extract_err fsync(fd);
    _return_normalized_err
}

int c_flush_data(const int fd) {
    _extract_err fdatasync(fd);
    _return_normalized_err
}

int c_seek(const int fd, const off_t offset, const int whence, off_t *restrict out_pos) {
    _extract_err lseek(fd, offset, whence);
    *out_pos = err < 0 ? -1 : err;
    _return_normalized_err
}

int c_tell(const int fd, off_t *restrict out_pos) {
    _extract_err lseek(fd, 0, SEEK_CUR);
    *out_pos = err < 0 ? -1 : err;
    _return_normalized_err
}

int c_truncate(const int fd, const off_t length) {
    _extract_err ftruncate(fd, length);
    _return_normalized_err
}

int c_lock_ex(const int fd, const bool non_blocking) {
    struct flock fl = {.l_type = F_WRLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = 0};
    _extract_err fcntl(fd, non_blocking ? F_SETLK : F_SETLKW, &fl);
    _return_normalized_err
}

int c_lock_sh(const int fd, const bool non_blocking) {
    struct flock fl = {.l_type = F_RDLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = 0};
    _extract_err fcntl(fd, non_blocking ? F_SETLK : F_SETLKW, &fl);
    _return_normalized_err
}

int c_unlock(const int fd) {
    struct flock fl = {.l_type = F_UNLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = 0};
    _extract_err fcntl(fd, F_SETLK, &fl);
    _return_normalized_err
}

int c_stat(const int fd, struct stat *restrict out) {
    _extract_err fstat(fd, out);
    _return_normalized_err
}

int c_dup(const int fd, int *restrict out_fd) {
    _extract_err dup(fd);
    *out_fd = err < 0 ? -1 : err;
    _return_normalized_err
}

int c_dup_into(const int fd, const int target_fd) {
    _extract_err dup2(fd, target_fd);
    _return_normalized_err
}

int c_pipe(int *restrict out_read_fd, int *restrict out_write_fd) {
    int fds[2];
    _extract_err pipe2(fds, O_CLOEXEC);
    if (err == 0) {
        *out_read_fd = fds[0];
        *out_write_fd = fds[1];
    }
    _return_normalized_err
}

int c_get_flags(const int fd, int *restrict out) {
    _extract_err fcntl(fd, F_GETFL);
    *out = err < 0 ? -1 : err;
    _return_normalized_err
}

int c_set_flags(const int fd, const int flags) {
    _extract_err fcntl(fd, F_SETFL, flags);
    _return_normalized_err
}

int c_get_status(const int fd, int *restrict out) {
    _extract_err fcntl(fd, F_GETFD);
    *out = err < 0 ? -1 : err;
    _return_normalized_err
}

int c_set_status(const int fd, const int flags) {
    _extract_err fcntl(fd, F_SETFD, flags);
    _return_normalized_err
}

int c_poll(struct pollfd *restrict fds, const nfds_t count, struct timespec const *restrict duration, int *restrict out_n) {
    _extract_err poll(fds, count, duration->tv_sec * 1000 + duration->tv_nsec / 1000000);
    *out_n = err < 0 ? -1 : err;
    _return_normalized_err
}

int c_readv(const int fd, struct iovec const *restrict iov, const int iov_count, ssize_t *restrict out_n) {
    _extract_err readv(fd, iov, iov_count);
    *out_n = err < 0 ? -1 : err;
    _return_normalized_err
}

int c_writev(const int fd, struct iovec const *restrict iov, const int iov_count, ssize_t *restrict out_n) {
    _extract_err writev(fd, iov, iov_count);
    *out_n = err < 0 ? -1 : err;
    _return_normalized_err
}

int c_pread(const int fd, void *restrict buffer, const size_t size, size_t count, const off_t offset, ssize_t *restrict out_n) {
    _extract_err pread(fd, buffer, size, offset);
    *out_n = err < 0 ? -1 : err;
    _return_normalized_err
}

int c_pwrite(const int fd, void const *restrict buffer, const size_t size, size_t count, const off_t offset, ssize_t *restrict out_n) {
    _extract_err pwrite(fd, buffer, size, offset);
    *out_n = err < 0 ? -1 : err;
    _return_normalized_err
}

int c_mmap(const int fd, const size_t length, const int prot, const int flags, const off_t offset, void *restrict out_addr, size_t *restrict out_length) {
    _extract_err mmap(out_addr, length, prot, flags, fd, offset);
    _return_normalized_void_err
}

int c_munmap(void *restrict addr, size_t const *restrict length) {
    _extract_err munmap(addr, *length);
    _return_normalized_err
}

int c_msync(void *restrict addr, size_t const *restrict length, const int flags) {
    _extract_err msync(addr, *length, flags);
    _return_normalized_err
}

int c_madvise(void *restrict addr, size_t const *restrict length, const int advice) {
    _extract_err madvise(addr, *length, advice);
    _return_normalized_err
}

int c_isatty(const int fd, bool *restrict out) {
    _extract_err isatty(fd);
    *out = err < 0 ? -1 : err;
    _return_normalized_err
}

int c_exists(char const *restrict path) {
    struct stat st = {};
    _extract_err lstat(path, &st);
    _return_normalized_err
}

int c_is_file(char const *restrict path, bool *restrict out) {
    struct stat st = {};
    _extract_err lstat(path, &st);
    if (err == 0) { *out = S_ISREG(st.st_mode); }
    _return_normalized_err
}

int c_is_dir(char const *restrict path, bool *restrict out) {
    struct stat st = {};
    _extract_err lstat(path, &st);
    if (err == 0) { *out = S_ISDIR(st.st_mode); }
    _return_normalized_err
}

int c_is_symlink(char const *restrict path, bool *restrict out) {
    struct stat st = {};
    _extract_err lstat(path, &st);
    if (err == 0) { *out = S_ISLNK(st.st_mode); }
    _return_normalized_err
}

int c_filesize(char const *restrict path, uint64_t *restrict out) {
    struct stat st = {};
    _extract_err lstat(path, &st);
    if (err == 0) { *out = st.st_size; }
    _return_normalized_err
}

int c_remove(char const *restrict path) {
    _extract_err remove(path);
    _return_normalized_err
}

int c_rename(char const *restrict old_path, char const *restrict new_path) {
    _extract_err rename(old_path, new_path);
    _return_normalized_err
}

int c_mkdir(char const *restrict path, const mode_t mode) {
    _extract_err mkdir(path, mode);
    _return_normalized_err
}

int c_rmdir(char const *restrict path) {
    _extract_err rmdir(path);
    _return_normalized_err
}

int c_chmod(char const *restrict path, const mode_t mode) {
    _extract_err chmod(path, mode);
    _return_normalized_err
}

int c_fsstat(char const *restrict path, struct stat *restrict out, const bool follow_symlink) {
    _extract_err (follow_symlink ? stat : lstat)(path, out);
    _return_normalized_err
}

int c_chown(char const *restrict path, const uid_t owner, const gid_t group, const bool follow_symlink) {
    _extract_err (follow_symlink ? chown : lchown)(path, owner, group);
    _return_normalized_err
}

int c_access(char const *restrict path, const int flags) {
    _extract_err access(path, flags);
    _return_normalized_err
}

int c_touch(char const *restrict path, const int flags) {
    _extract_err utimensat(AT_FDCWD, path, NULL, flags);
    _return_normalized_err
}

int c_realpath(char const *restrict path, char *restrict buffer) {
    _extract_err realpath(path, buffer);
    _return_normalized_err
}

int c_hardlink(char const *restrict target, char const *restrict linkpath) {
    _extract_err link(target, linkpath);
    _return_normalized_err
}

int c_symlink(char const *restrict target, char const *restrict linkpath) {
    _extract_err symlink(target, linkpath);
    _return_normalized_err
}

int c_readlink(char const *restrict path, char *restrict buffer, size_t buffer_size) {
    _extract_err readlink(path, buffer, 256);
    _return_normalized_err
}

int c_mktemp(char *restrict path, int *restrict out_fd) {
    _extract_err mkostemp(path, O_CLOEXEC);
    *out_fd = err < 0 ? -1 : err;
    _return_normalized_err
}

int c_mktemp_dir(char *restrict path) {
    _extract_err mkdtemp(path);
    _return_normalized_err
}

int c_statvfs(char const *restrict path, struct statvfs *restrict out) {
    _extract_err statvfs(path, out);
    _return_normalized_err
}

int c_copy_file_range(const int fd_in, off_t *restrict off_in, const int fd_out, off_t *restrict off_out, const size_t len, const int flags) {
    _extract_err copy_file_range(fd_in, off_in, fd_out, off_out, len, flags);
    _return_normalized_err
}

int c_getcwd(char *restrict buffer, const size_t size) {
    _extract_err getcwd(buffer, size);
    _return_normalized_err
}

int c_chdir(char const *restrict path) {
    _extract_err chdir(path);
    _return_normalized_err
}

void* c_malloc(const size_t size) {
    _extract_err malloc(size);
    _return_pointer
}

void* c_aligned_alloc(const size_t size, const size_t alignment) {
    _extract_err aligned_alloc(size, alignment);
    _return_pointer
}

void* c_calloc(const size_t num, const size_t size) {
    _extract_err calloc(num, size);
    _return_pointer
}

void* c_realloc(void *ptr, const size_t new_size) {
    _extract_err realloc(ptr, new_size);
    _return_pointer
}

void c_free(void *ptr) {
    free(ptr);
}

int c_memcpy(void *restrict dest, void const *restrict src, const size_t size) {
    _extract_err memcpy(dest, src, size);
    _return_normalized_err
}

int c_memmove(void *restrict dest, void const *restrict src, const size_t size) {
    _extract_err memmove(dest, src, size);
    _return_normalized_err
}

int c_memset(void *dest, const int value, const size_t size) {
    _extract_err memset(dest, value, size);
    _return_normalized_err
}

int c_memcmp(void const *ptr1, void const *ptr2, const size_t size, int *restrict out) {
    _extract_err memcmp(ptr1, ptr2, size);
    *out = err < 0 ? -1 : err;
    _return_normalized_err
}

int c_memcmpconst(void const *ptr1, void const *ptr2, const size_t size, int *restrict out) {
    const volatile unsigned char *a = ptr1;
    const volatile unsigned char *b = ptr2;
    unsigned char result = 0;
    for (size_t i = 0; i < size; i++) { result |= a[i] ^ b[i]; }
    *out = result;
    return 0;
}

int c_memmem(void const *haystack, const size_t haystack_size, void const *needle, const size_t needle_size, size_t *restrict out) {
    _extract_err memmem(haystack, haystack_size, needle, needle_size);
    if (err != NULL) { *out = (size_t)((char*)err - (char*)haystack); }
    _return_normalized_void_err
}

int c_mlock(const void *addr, const size_t size) {
    _extract_err mlock(addr, size);
    _return_normalized_err
}

int c_munlock(const void *addr, const size_t size) {
    _extract_err munlock(addr, size);
    _return_normalized_err
}

int c_memprotect(void *addr, const size_t size, const int prot) {
    _extract_err mprotect(addr, size, prot);
    _return_normalized_err
}

int c_strcpy(char *restrict dest, char const *restrict src) {
    _extract_err strcpy(dest, src);
    _return_normalized_ptr_err
}

int c_strcat(char *restrict dest, char const *restrict src) {
    _extract_err strcat(dest, src);
    _return_normalized_err
}

int c_strcmp(char const *str1, char const *str2, bool *restrict out) {
    _extract_err strcmp(str1, str2);
    *out = err == 0 ? true : false;
    _return_normalized_err
}

int c_strcasecmp(char const *str1, char const *str2, bool *restrict out) {
    _extract_err strcasecmp(str1, str2);
    *out = err == 0 ? true : false;
    _return_normalized_err
}

int c_strchr(char const *str, const char ch, int *restrict out_idx) {
    _extract_err strchr(str, ch);
    if (err != NULL) { *out_idx = (int)(err - str); }
    _return_normalized_void_err
}

int c_strrchr(char const *str, const char ch, int *restrict out_idx) {
    _extract_err strrchr(str, ch);
    if (err != NULL) { *out_idx = (int)(err - str); }
    _return_normalized_void_err
}

int c_strstr(char const *haystack, char const *needle, int *restrict out_idx) {
    _extract_err strstr(haystack, needle);
    if (err != NULL) { *out_idx = (int)(err - haystack); }
    _return_normalized_void_err
}

int c_strrstr(char const *haystack, char const *needle, int *restrict out_idx) {
    _extract_err strrstr(haystack, needle);
    if (err != NULL) { *out_idx = (int)(err - haystack); }
    _return_normalized_void_err
}

int c_strcasestr(char const *haystack, char const *needle, int *restrict out_idx) {
    _extract_err strcasestr(haystack, needle);
    if (err != NULL) { *out_idx = (int)(err - haystack); }
    _return_normalized_void_err
}

int c_strpbrk(char const *string, char const *accept, int *restrict out_idx) {
    _extract_err strpbrk(string, accept);
    if (err != NULL) { *out_idx = (int)(err - string); }
    _return_normalized_void_err
}

void* c_strdup(char const *str) {
    _extract_err strdup(str);
    _return_pointer
}

int c_get_pid(pid_t *restrict out_pid) {
    *out_pid = getpid();
    _return_success
}

int c_get_ppid(pid_t *restrict out_ppid) {
    *out_ppid = getppid();
    _return_success
}

int c_getenv(char const *restrict key, char *restrict out) {
    _extract_err secure_getenv(key);
    *out = err != NULL ? *err : '\0';
    _return_normalized_ptr_err
}

int c_setenv(char const *restrict key, char const *restrict val, const bool overwrite) {
    _extract_err setenv(key, val, overwrite ? 1 : 0);
    _return_normalized_err
}

int c_unsetenv(char const *restrict key) {
    _extract_err unsetenv(key);
    _return_normalized_err
}

int c_signal(const pid_t pid, const int signal) {
    _extract_err kill(pid, signal);
    _return_normalized_err
}

int c_is_running(const pid_t pid) {
    _extract_err kill(pid, 0);
    if (err == 0) { return 1; }
    if (err == ESRCH) { return 0; }
    _return_normalized_err
}

void c_exit(const int status) {
    exit(status);
}

void c_exit_clean(const int status) {
    c_cleanup();
    exit(status);
}

void c_abort() {
    abort();
}

int c_set_sockaddr_storage_v4(uint8_t const *octets, const uint16_t port, struct sockaddr_storage *restrict out_storage) {
    const auto addr = (struct sockaddr_in*)out_storage;
    addr->sin_family = AF_INET;
    addr->sin_port = htons(port);
    memset(&addr->sin_zero, 0, sizeof(addr->sin_zero));
    memcpy(&addr->sin_addr.s_addr, octets, 4);
    return 0;
}

int c_set_sockaddr_storage_v6(uint16_t const *segments, const uint16_t port, struct sockaddr_storage *restrict out_storage) {
    const auto addr = (struct sockaddr_in6*)out_storage;
    addr->sin6_family = AF_INET6;
    addr->sin6_port = htons(port);
    addr->sin6_flowinfo = 0;
    addr->sin6_scope_id = 0;
    memcpy(&addr->sin6_addr.s6_addr, segments, 16);
    return 0;
}

int c_get_sockaddr_storage_v4(struct sockaddr_storage const *restrict storage, uint8_t *out_octets, uint16_t *out_port) {
    const auto addr = (struct sockaddr_in*)storage;
    memcpy(out_octets, &addr->sin_addr.s_addr, 4);
    *out_port = ntohs(addr->sin_port);
    return 0;
}

int c_get_sockaddr_storage_v6(struct sockaddr_storage const *restrict storage, uint16_t *out_segments, uint16_t *out_port) {
    const auto addr = (struct sockaddr_in6*)storage;
    memcpy(out_segments, &addr->sin6_addr.s6_addr, 16);
    *out_port = ntohs(addr->sin6_port);
    return 0;
}

int c_sockaddr_family(struct sockaddr_storage const *restrict storage, int *restrict out_family) {
    *out_family = storage->ss_family;
    _return_success
}

int c_socket(const int domain, const int type, const int protocol, int *restrict out_fd) {
    _extract_err socket(domain, type, protocol);
    *out_fd = err < 0 ? -1 : err;
    _return_normalized_err
}

int c_shutdown(const int fd, const int how) {
    _extract_err shutdown(fd, how);
    _return_normalized_err
}

int c_connect(const int fd, struct sockaddr_storage const *restrict storage) {
    _socket_addr_in_construction_helper
    _extract_err connect(fd, (struct sockaddr*)storage, len);
    _return_normalized_err
}

int c_bind(const int fd, struct sockaddr_storage const *restrict storage) {
    _socket_addr_in_construction_helper
    _extract_err bind(fd, (struct sockaddr*)storage, len);
    _return_normalized_err
}

int c_listen(const int fd, const int backlog) {
    _extract_err listen(fd, backlog);
    _return_normalized_err
}

int c_accept(const int fd, struct sockaddr_storage *restrict out_storage, int *restrict out_fd) {
    _socket_addr_out_construction_helper
    _extract_err accept4(fd, (struct sockaddr*)out_storage, &len, O_CLOEXEC);
    *out_fd = err < 0 ? -1 : err;
    _return_normalized_err
}

int c_send(const int fd, char const *data, const size_t size, const int flags, ssize_t *restrict out_n) {
    _extract_err send(fd, data, size, flags);
    *out_n = err < 0 ? -1 : err;
    _return_normalized_err
}

int c_recv(const int fd, char *buffer, const size_t size, const int flags, ssize_t *restrict out) {
    _extract_err recv(fd, buffer, size, flags);
    *out = err < 0 ? -1 : err;
    _return_normalized_err
}

int c_sendto(const int fd, char const *data, const size_t size, struct sockaddr_storage const *restrict storage, ssize_t *restrict out_n) {
    _socket_addr_in_construction_helper
    _extract_err sendto(fd, data, size, 0, (struct sockaddr*)storage, len);
    *out_n = err < 0 ? -1 : err;
    _return_normalized_err
}

int c_recvfrom(const int fd, char *buffer, const size_t size, struct sockaddr_storage *restrict out_storage, ssize_t *restrict out_n) {
    _socket_addr_out_construction_helper
    _extract_err recvfrom(fd, buffer, size, 0, (struct sockaddr*)out_storage, &len);
    *out_n = err < 0 ? -1 : err;
    _return_normalized_err
}

int c_set_nonblocking(const int fd, const bool non_blocking) {
    const auto flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) { return -1; }
    _extract_err fcntl(fd, F_SETFL, non_blocking ? flags | O_NONBLOCK : flags & ~O_NONBLOCK);
    _return_normalized_err
}

int c_set_keepalive(const int fd, const bool keepalive) {
    _extract_err setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
    _return_normalized_err
}

int c_set_linger(const int fd, bool on, struct timeval const *restrict linger) {
    const struct linger l = {.l_onoff = linger ? 1 : 0, .l_linger = linger->tv_sec};
    _extract_err setsockopt(fd, SOL_SOCKET, SO_LINGER, &l, sizeof(l));
    _return_normalized_err
}

int c_set_nodelay(const int fd, const bool nodelay) {
    _extract_err setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
    _return_normalized_err
}

int c_set_ttl(const int fd, const int ttl) {
    _extract_err setsockopt(fd, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl));
    _return_normalized_err
}

int c_set_broadcast(const int fd, const bool broadcast) {
    _extract_err setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));
    _return_normalized_err
}

int c_set_recv_timeout(const int fd, struct timeval const *restrict timeout) {
    _extract_err setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (void*)&timeout, sizeof(timeout));
    _return_normalized_err
}

int c_set_send_timeout(const int fd, struct timeval const *restrict timeout) {
    _extract_err setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (void*)&timeout, sizeof(timeout));
    _return_normalized_err
}

int c_get_keepalive(const int fd, bool *restrict out_keepalive) {
    auto len = (socklen_t)sizeof(bool);
    _extract_err getsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, out_keepalive, &len);
    _return_normalized_err
}

int c_get_linger(const int fd, bool *restrict out_on, struct timeval *restrict out_linger) {
    struct linger l = {};
    auto len = (socklen_t)sizeof(l);
    _extract_err getsockopt(fd, SOL_SOCKET, SO_LINGER, &l, &len);
    if (err == 0) {
        *out_on = l.l_onoff != 0;
        out_linger->tv_sec = l.l_linger;
        out_linger->tv_usec = 0;
    }
    _return_normalized_err
}

int c_get_nodelay(const int fd, bool *restrict out_nodelay) {
    auto len = (socklen_t)sizeof(bool);
    _extract_err getsockopt(fd, IPPROTO_TCP, TCP_NODELAY, out_nodelay, &len);
    _return_normalized_err
}

int c_get_ttl(const int fd, int *restrict out_ttl) {
    auto len = (socklen_t)sizeof(int);
    _extract_err getsockopt(fd, IPPROTO_IP, IP_TTL, out_ttl, &len);
    _return_normalized_err
}

int c_get_broadcast(const int fd, bool *restrict out_broadcast) {
    auto len = (socklen_t)sizeof(bool);
    _extract_err getsockopt(fd, SOL_SOCKET, SO_BROADCAST, out_broadcast, &len);
    _return_normalized_err
}

int c_get_recv_timeout(const int fd, struct timeval *restrict out_timeout) {
    auto len = (socklen_t)sizeof(int);
    _extract_err getsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, out_timeout, &len);
    _return_normalized_err
}

int c_get_send_timeout(const int fd, struct timeval *restrict out_timeout) {
    auto len = (socklen_t)sizeof(int);
    _extract_err getsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, out_timeout, &len);
    _return_normalized_err
}

int c_getsockname(const int fd, struct sockaddr_storage *restrict out_storage) {
    _socket_addr_out_construction_helper
    _extract_err getsockname(fd, (struct sockaddr*)out_storage, &len);
    _return_normalized_err
}

int c_getpeername(const int fd, struct sockaddr_storage *restrict out_storage) {
    _socket_addr_out_construction_helper
    _extract_err getpeername(fd, (struct sockaddr*)out_storage, &len);
    _return_normalized_err
}

int c_clockgettime(const clockid_t clock_id, struct timespec *restrict out_tp) {
    _extract_err clock_gettime(clock_id, out_tp);
    _return_normalized_err
}

int c_sleep(const clockid_t clock, const int flags, struct timespec const *restrict duration) {
    _extract_err clock_nanosleep(clock, flags, duration, NULL);
    _return_normalized_err
}

int c_prngseed(const uint64_t seed) {
    _extract_err prng_init_from_seed(&SPPC_PRNG_STATE, seed);
    _return_normalized_err
}

int c_prngreset(void) {
    _extract_err prng_init_from_os(&SPPC_PRNG_STATE);
    _return_normalized_err
}

int c_prngbytes(const size_t size, void *restrict out) {
    for (size_t i = 0; i < size; i += 8) {
        auto rand_val = xoshiro256ss(SPPC_PRNG_STATE.state);
        const auto bytes_to_copy = size - i < 8 ? size - i : 8;
        memcpy((uint8_t*)out + i, &rand_val, bytes_to_copy);
    }
    _return_success
}

int c_prngu64(uint64_t *restrict out) {
    *out = xoshiro256ss(SPPC_PRNG_STATE.state);
    _return_success
}

int c_prngu32(uint32_t *restrict out) {
    *out = (uint32_t)xoshiro256ss(SPPC_PRNG_STATE.state);
    _return_success
}

int c_prngdouble(double *restrict out) {
    *out = (double)(xoshiro256ss(SPPC_PRNG_STATE.state) >> 11) * (1.0 / (1ULL << 53));
    _return_success
}

int c_csprngbytes(const size_t size, void *restrict out) {
    _extract_err getrandom(out, size, 0);
    _return_success
}

int c_csprngu32(uint32_t *restrict out) {
    _extract_err getrandom(out, sizeof(uint32_t), 0);
    _return_success
}

int c_csprngu64(uint64_t *restrict out) {
    _extract_err getrandom(out, sizeof(uint64_t), 0);
    _return_success
}
