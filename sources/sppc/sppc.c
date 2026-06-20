#define _GNU_SOURCE
#include <sppc/sppc.h>
#include <errno.h>        // error number definitions
#include <fcntl.h>        // file operations and macros
#include <poll.h>         // polling operations and macros
#include <libgen.h>       // path manipulation functions and macros
#include <locale.h>       // locale functions and macros
#include <malloc.h>       // memory allocation functions and macros
#include <netdb.h>        // network database operations and macros
#include <stdlib.h>       // memory allocation functions
#include <stdio.h>        // standard I/O operations and macros
#include <string.h>       // string operations and macros
#include <unistd.h>       // stream operations and macros
#include <sys/file.h>     // file locking operations and macros
#include <sys/mman.h>     // memory mapping operations and macros
#include <sys/random.h>   // random number generation functions and macros
#include <sys/resource.h> // resource operations and macros
#include <sys/socket.h>   // socket operations and macros
#include <sys/stat.h>     // file status operations and macros
#include <sys/statvfs.h>  // file system information operations and macros
#include <sys/sysinfo.h>  // system information functions and macros
#include <sys/uio.h>      // readv and writev
#include <sys/utsname.h>  // system information functions and macros
#include <sys/wait.h>     // process control operations and macros
#include <arpa/inet.h>    // internet operations and macros
#include <netinet/tcp.h>  // TCP protocol definitions
#include <time.h>         // time functions and macros
#include <pthread.h>      // POSIX threads operations and macros
#include <stdint.h>       // uintptr_t

// How to do the error checks:
//  1. NULL checks
//  2. Value checks: == 0, < 0
//  3. Calculation checks: value < size * count

typedef struct {
    uint64_t s[4];
} prng_state_t;

static constexpr auto SPPC_PATH_MAX = 4096;
static _Thread_local prng_state_t SPPC_PRNG_STATE = {0};
static _Thread_local auto SPPC_PRNG_SEEDED = false;

// static _Thread_local auto LOCAL_ERRNO = 0; // file descriptors
// #define local_errno LOCAL_ERRNO

#define _so_address_helper(socktype, proto, bind, cond)              \
    char port_str[6];                                                \
    snprintf(port_str, sizeof(port_str), "%u", addr->port);          \
                                                                     \
    struct addrinfo hints = {};                                      \
    memset(&hints, 0, sizeof(hints));                                \
    hints.ai_family = AF_UNSPEC;                                     \
    hints.ai_socktype = socktype;                                    \
    hints.ai_protocol = proto;                                       \
    if (bind) { hints.ai_flags = AI_PASSIVE; }                       \
                                                                     \
    struct addrinfo *res = NULL;                                     \
    const auto _gai_err = getaddrinfo(addr->host, port_str, &hints, &res); \
    if (_gai_err != 0) { return _gai_err; }                          \
                                                                     \
    auto _ret = -1;                                                  \
    for (auto ai = res; ai != NULL; ai = ai->ai_next) {              \
        if (cond) { _ret = 0; break; }                               \
    }                                                                \
                                                                     \
    freeaddrinfo(res);                                               \
    return _ret;

#define _so_address_return_helper(sock, unknown_handler)                        \
    if (addr != NULL) {                                                         \
        addr->family = sock.ss_family;                                          \
        if (sock.ss_family == AF_INET) {                                        \
            const auto s = (struct sockaddr_in*)&sock;                          \
            inet_ntop(AF_INET, &s->sin_addr, addr->host, sizeof(addr->host));   \
            addr->port = ntohs(s->sin_port);                                    \
        }                                                                       \
        else if (sock.ss_family == AF_INET6) {                                  \
            const auto s = (struct sockaddr_in6*)&sock;                         \
            inet_ntop(AF_INET6, &s->sin6_addr, addr->host, sizeof(addr->host)); \
            addr->port = ntohs(s->sin6_port);                                   \
        }                                                                       \
        else {                                                                  \
            unknown_handler                                                     \
            return EINVAL;                                                      \
        }                                                                       \
    }

#define _pt_timeout_helper(what, op, ...)              \
    struct timespec abs_timeout;                       \
    clock_gettime(CLOCK_REALTIME, &abs_timeout);       \
    abs_timeout.tv_sec += timeout->seconds;            \
    abs_timeout.tv_nsec += timeout->nanoseconds;       \
    if (abs_timeout.tv_nsec >= 1000000000) {           \
        abs_timeout.tv_sec += 1;                       \
        abs_timeout.tv_nsec -= 1000000000;             \
    }                                                  \
    const auto err = pthread_ ## what ## _timed ## op( \
        (pthread_ ## what ## _t*)(uintptr_t)handle     \
        __VA_OPT__(, __VA_ARGS__), &abs_timeout);      \
    if (err == 0) { return 1; }                        \
    if (err == ETIMEDOUT) { return 0; }                \
    return err;

#define _sret_finish_helper(value, ...) \
    *out = value __VA_OPT__(, __VA_ARGS__); \
    return 0;

#define _ret_error_helper(expr, error) \
    if (expr) { return error; }

#define _struct_builder(type, var, ...) \
    struct type var = { __VA_ARGS__ };

#define _eintr_repeat_assign(type, var, expr, ...) \
    type var; do { var = expr; } while (var == -1 && errno == EINTR __VA_OPT__( && __VA_ARGS__));

#define _standard_assign(var, expr, ...) \
    var = expr __VA_OPT__(, __VA_ARGS__);

#define _ret_finish_helper() \
    return 0;

#define _exit_when(when, code) \
    if (when) { _exit(code); }

static int _prng_ensure_seeded(void) {
    if (SPPC_PRNG_SEEDED) { return 0; }
    if (rn_csprng_bytes(SPPC_PRNG_STATE.s, sizeof(SPPC_PRNG_STATE.s)) == -1) { return -1; }
    SPPC_PRNG_SEEDED = true;
    return 0;
}

static uint64_t _rotl(const uint64_t x, const int k) {
    return x << k | x >> (64 - k);
}

static uint64_t _xoshiro256ss_next(uint64_t s[4]) {
    const auto result = _rotl(s[1] * 5, 7) * 9;
    const auto t = s[1] << 17;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] ^= _rotl(s[3], 5);
    return result;
}

void init_c(void) {
    signal(SIGPIPE, SIG_IGN); // let write() return EPIPE instead of killing the process when writing to a closed fd.
    signal(SIGCHLD, SIG_DFL); // ensure zombie reaping works correctly.
    signal(SIGHUP,  SIG_IGN); // ignore SIGHUP to prevent accidental termination when the controlling terminal is closed.
    _prng_ensure_seeded();
    setlocale(LC_ALL, "");
    tzset();
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    mallopt(M_TRIM_THRESHOLD, 128 * 1024);  // trim after 128KB free
    mallopt(M_MMAP_THRESHOLD, 64  * 1024);  // mmap allocations above 64KB

    // struct rlimit rl;
    // if (getrlimit(RLIMIT_NOFILE, &rl) == 0) {
    //     rl.rlim_cur = rl.rlim_max;
    //     setrlimit(RLIMIT_NOFILE, &rl);
    // }

    // struct rlimit core_rl;
    // if (getrlimit(RLIMIT_CORE, &core_rl) == 0) {
    //     core_rl.rlim_cur = core_rl.rlim_max;
    //     setrlimit(RLIMIT_CORE, &core_rl);
    // }
}

void cleanup_c(void) {
    signal(SIGPIPE, SIG_DFL);
    signal(SIGHUP,  SIG_DFL);
    sync();
}

int fd_read(void *restrict buffer, const size_t size, const size_t count, const int fd, ssize_t *restrict out) {
    _ret_error_helper(buffer == NULL || out == NULL, EINVAL)
    _ret_error_helper(size == 0 || count == 0 || fd < 0, EINVAL)
    _ret_error_helper(count > SIZE_MAX / size, EOVERFLOW)
    _eintr_repeat_assign(ssize_t, n, read(fd, buffer, size * count))
    _ret_error_helper(n == -1, errno)
    _sret_finish_helper(n)
}

int fd_write(void const *restrict buffer, const size_t size, const size_t count, const int fd, ssize_t *restrict out) {
    _ret_error_helper(buffer == NULL || out == NULL, EINVAL)
    _ret_error_helper(size == 0 || count == 0 || fd < 0, EINVAL)
    _ret_error_helper(count > SIZE_MAX / size, EOVERFLOW)
    _eintr_repeat_assign(ssize_t, n, write(fd, buffer, size * count))
    _ret_error_helper(n == -1, errno)
    _sret_finish_helper(n)
}

int fd_open(char const *restrict path, const int flags, const mode_t mode, int *restrict out) {
    _ret_error_helper(path == NULL || out == NULL, EINVAL)
    _ret_error_helper(mode > 0777, EINVAL)
    _ret_error_helper(flags & ~(O_RDONLY | O_WRONLY | O_RDWR | O_CREAT | O_EXCL | O_TRUNC | O_APPEND | O_CLOEXEC), EINVAL)
    _eintr_repeat_assign(ssize_t, n, open(path, flags, mode))
    _ret_error_helper(n == -1, errno)
    _sret_finish_helper(n)
}

int fd_close(const int fd) {
    _ret_error_helper(fd < 0, EINVAL)
    _standard_assign(const auto err, close(fd))
    _ret_error_helper(err == -1, errno)
    _ret_finish_helper()
}

int fd_flush(const int fd) {
    _ret_error_helper(fd < 0, EINVAL)
    _standard_assign(const auto err, fsync(fd))
    _ret_error_helper(err == -1, errno)
    _ret_finish_helper()
}

int fd_flush_data(const int fd) {
    _ret_error_helper(fd < 0, EINVAL)
    _standard_assign(const auto err, fdatasync(fd))
    _ret_error_helper(err == -1, errno)
    _ret_finish_helper()
}

int fd_seek(const int fd, const off_t offset, const int whence, off_t *restrict out) {
    _ret_error_helper(out == NULL, EINVAL)
    _ret_error_helper(whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END, EINVAL)
    _standard_assign(const auto n, lseek(fd, offset, whence))
    _ret_error_helper(n == -1, errno)
    _sret_finish_helper(n)
}

int fd_tell(const int fd, off_t *restrict out) {
    _ret_error_helper(out == NULL, EINVAL)
    _ret_error_helper(fd < 0, EINVAL)
    _standard_assign(const auto n, lseek(fd, 0, SEEK_CUR))
    _ret_error_helper(n == -1, errno)
    _sret_finish_helper(n)
}

int fd_truncate(const int fd, const off_t length) {
    _ret_error_helper(fd < 0 || length < 0, EINVAL)
    _standard_assign(const auto err, ftruncate(fd, length))
    _ret_error_helper(err == -1, errno)
    _ret_finish_helper()
}

int fd_lock_ex(const int fd, const bool non_blocking) {
    _ret_error_helper(fd < 0, EINVAL)
    _struct_builder(flock, fl, .l_type = F_WRLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = 0)
    _eintr_repeat_assign(ssize_t, err, fcntl(fd, non_blocking ? F_SETLK : F_SETLKW, &fl), !non_blocking)
    _ret_finish_helper()
}

int fd_lock_sh(const int fd, const bool non_blocking) {
    _ret_error_helper(fd < 0, EINVAL)
    _struct_builder(flock, fl, .l_type = F_RDLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = 0)
    _eintr_repeat_assign(ssize_t, err, fcntl(fd, non_blocking ? F_SETLK : F_SETLKW, &fl), !non_blocking)
    _ret_finish_helper()
}

int fd_unlock(const int fd) {
    _ret_error_helper(fd < 0, EINVAL)
    _struct_builder(flock, fl, .l_type = F_UNLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = 0)
    _eintr_repeat_assign(ssize_t, err, fcntl(fd, F_SETLK, &fl))
    _ret_finish_helper()
}

int fd_stat(const int fd, fd_stat_t *restrict out) {
    _ret_error_helper(out == NULL, EINVAL)
    _ret_error_helper(fd < 0, EINVAL)
    _struct_builder(stat, raw)
    _standard_assign(const auto err, fstat(fd, &raw))
    _ret_error_helper(err == -1, errno)
    _standard_assign(*out, (fd_stat_t){
        .size = raw.st_size,
        .blocks = raw.st_blocks,
        .block_size = raw.st_blksize,
        .mode = raw.st_mode,
        .uid = raw.st_uid,
        .gid = raw.st_gid,
        .atime = raw.st_atime,
        .mtime = raw.st_mtime,
        .ctime = raw.st_ctime
    })
    _ret_finish_helper()
}

int fd_dup(const int fd, int *restrict out) {
    _ret_error_helper(fd < 0 || out == NULL, EINVAL)
    _standard_assign(const auto new_fd, dup(fd))
    _sret_finish_helper(new_fd)
}

int fd_dup_into(const int fd, const int target_fd) {
    _ret_error_helper(fd < 0 || target_fd < 0, EINVAL)
    _eintr_repeat_assign(int, err, dup2(fd, target_fd))
    _ret_error_helper(err == -1, errno)
    _ret_finish_helper()
}

int fd_pipe(int *restrict out_read_fd, int *restrict out_write_fd) {
    _ret_error_helper(out_read_fd == NULL || out_write_fd == NULL, EINVAL)
    int fds[2];
    _standard_assign(const auto err, pipe2(fds, O_CLOEXEC))
    _ret_error_helper(err == -1, errno)
    _standard_assign(*out_read_fd, fds[0])
    _standard_assign(*out_write_fd, fds[1])
    _ret_finish_helper()
}

int fd_get_flags(const int fd, int *restrict out) {
    _ret_error_helper(fd < 0, EINVAL)
    _ret_error_helper(out == NULL, EINVAL)
    _standard_assign(const auto err, fcntl(fd, F_GETFL))
    _ret_error_helper(err == -1, errno)
    _sret_finish_helper(err)
}

int fd_set_flags(const int fd, const int flags) {
    _ret_error_helper(fd < 0 || flags < 0, EINVAL)
    _standard_assign(const auto err, fcntl(fd, F_SETFL, flags))
    _ret_error_helper(err, errno)
    _ret_finish_helper()
}

int fd_get_status(const int fd, int *restrict out) {
    _ret_error_helper(out == NULL, EINVAL)
    _ret_error_helper(fd < 0, EINVAL)
    _standard_assign(const auto err, fcntl(fd, F_GETOWN))
    _ret_error_helper(err == -1, errno)
    _sret_finish_helper(err)
}

int fd_set_status(const int fd, const int flags) {
    _ret_error_helper(fd < 0 || flags < 0, EINVAL)
    _standard_assign(const auto err, fcntl(fd, F_SETOWN, flags))
    _ret_error_helper(err == -1, errno)
    _ret_finish_helper()
}

int fd_poll(fd_poll_t *restrict fds, const nfds_t count, const int timeout_ms, int *restrict out) {
    _ret_error_helper(fds == NULL || out == NULL, EINVAL)
    _ret_error_helper(count == 0, EINVAL)
    _eintr_repeat_assign(int, err, poll((struct pollfd*)fds, count, timeout_ms))
    _ret_error_helper(err == -1, errno)
    _sret_finish_helper(err)
}

int fd_readv(const int fd, fd_iovec_t const *restrict iov, const int iov_count, ssize_t *restrict out) {
    _ret_error_helper(iov == NULL || out == NULL, EINVAL)
    _ret_error_helper(fd < 0 || iov_count <= 0, EINVAL)
    _eintr_repeat_assign(int, err, readv(fd, (const struct iovec*)iov, iov_count))
    _ret_error_helper(err == -1, errno)
    _sret_finish_helper(err)
}

int fd_writev(const int fd, fd_iovec_t const *restrict iov, const int iov_count, ssize_t *restrict out) {
    _ret_error_helper(iov == NULL || out == NULL, EINVAL)
    _ret_error_helper(fd < 0 || iov_count <= 0, EINVAL)
    _eintr_repeat_assign(int, err, writev(fd, (const struct iovec*)iov, iov_count))
    _ret_error_helper(err == -1, errno)
    _sret_finish_helper(err)
}

int fd_pread(const int fd, void *restrict buffer, const size_t size, const size_t count, const off_t offset, ssize_t *restrict out) {
    _ret_error_helper(buffer == NULL || out == NULL, EINVAL)
    _ret_error_helper(fd < 0 || size <= 0 || count <= 0, EINVAL)
    _ret_error_helper(count > SIZE_MAX / size, EOVERFLOW)
    _eintr_repeat_assign(ssize_t, n, pread(fd, buffer, size * count, offset))
    _ret_error_helper(n == -1, errno)
    _sret_finish_helper(n)
}

int fd_pwrite(const int fd, void const *restrict buffer, const size_t size, const size_t count, const off_t offset, ssize_t *restrict out) {
    _ret_error_helper(buffer == NULL || out == NULL, EINVAL)
    _ret_error_helper(fd < 0 || size <= 0 || count <= 0, EINVAL)
    _ret_error_helper(count > SIZE_MAX / size, EOVERFLOW)
    _eintr_repeat_assign(ssize_t, n, pwrite(fd, buffer, size * count, offset))
    _ret_error_helper(n == -1, errno)
    _sret_finish_helper(n)
}

int fd_mmap(const int fd, const size_t length, const int prot, const int flags, const off_t offset, fd_mmap_t *restrict out) {
    _ret_error_helper(out == NULL, EINVAL)
    _ret_error_helper(fd < 0 || length == 0 || offset < 0, EINVAL)
    _ret_error_helper(prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC), EINVAL)
    _ret_error_helper(flags & ~(MAP_SHARED | MAP_PRIVATE | MAP_ANONYMOUS), EINVAL)
    _standard_assign(const auto addr, mmap(NULL, length, prot, flags, fd, offset))
    _ret_error_helper(addr == MAP_FAILED, errno)
    _standard_assign(*out, (fd_mmap_t){.addr = addr, .length = length})
    _ret_finish_helper()
}

int fd_munmap(fd_mmap_t *restrict mmap) {
    _ret_error_helper(mmap == NULL || mmap->addr == NULL, EINVAL)
    _ret_error_helper(mmap->length == 0, EINVAL)
    _standard_assign(const auto err, munmap(mmap->addr, mmap->length))
    _ret_error_helper(err == -1, errno)
    _standard_assign(*mmap, (fd_mmap_t){ .addr = NULL, .length = 0 });
    _ret_finish_helper()
}

int fd_msync(fd_mmap_t const *restrict mmap, const size_t length, const int flags) {
    _ret_error_helper(mmap == NULL || mmap->addr == NULL, EINVAL)
    _ret_error_helper(length == 0, EINVAL)
    _ret_error_helper(flags & ~(MS_ASYNC | MS_SYNC | MS_INVALIDATE), EINVAL)
    _standard_assign(const auto err, msync(mmap->addr, length, flags))
    _ret_error_helper(err == -1, errno)
    _ret_finish_helper();
}

int fd_madvise(fd_mmap_t const *restrict mmap, const size_t length, const int advice) {
    _ret_error_helper(mmap == NULL || mmap->addr == NULL, EINVAL)
    _ret_error_helper(length == 0, EINVAL)
    _ret_error_helper(advice & ~(MADV_NORMAL | MADV_RANDOM | MADV_SEQUENTIAL | MADV_WILLNEED | MADV_DONTNEED), EINVAL)
    _standard_assign(const auto err, madvise(mmap->addr, length, advice))
    _ret_error_helper(err == -1, errno)
    _ret_finish_helper();
}

int fs_exists(char const *restrict path) {
    _ret_error_helper(path == NULL, EINVAL)
    _struct_builder(stat, st)
    _standard_assign(const auto err, lstat(path, &st))
    _ret_error_helper(errno == ENOENT || errno == ENOTDIR, 0)
    _ret_error_helper(err == -1, errno)
    return 1;
}

int fs_is_file(char const *restrict path) {
    _ret_error_helper(path == NULL, EINVAL)
    _struct_builder(stat, st)
    _standard_assign(const auto err, lstat(path, &st))
    _ret_error_helper(err == -1, errno)
    return S_ISREG(st.st_mode);
}

int fs_is_dir(char const *restrict path) {
    _ret_error_helper(path == NULL, EINVAL)
    _struct_builder(stat, st)
    _standard_assign(const auto err, lstat(path, &st))
    _ret_error_helper(err == -1, errno)
    return S_ISDIR(st.st_mode);
}

int fs_is_symlink(char const *restrict path) {
    _ret_error_helper(path == NULL, EINVAL)
    _struct_builder(stat, st)
    _standard_assign(const auto err, lstat(path, &st))
    _ret_error_helper(err == -1, errno)
    return S_ISLNK(st.st_mode);
}

int fs_filesize(char const *restrict path, uint64_t *restrict out) {
    _ret_error_helper(path == NULL || out == NULL, EINVAL)
    _struct_builder(stat, st)
    _standard_assign(const auto err, lstat(path, &st))
    _ret_error_helper(err == -1, errno)
    _ret_error_helper(!S_ISREG(st.st_mode), EINVAL)
    _sret_finish_helper(st.st_size)
}

int fs_remove(char const *restrict path) {
    _ret_error_helper(path == NULL, EINVAL)
    _standard_assign(const auto err, remove(path))
    _ret_error_helper(err == -1, errno)
    _ret_finish_helper()
}

int fs_rename(char const *restrict old_path, char const *restrict new_path) {
    _ret_error_helper(old_path == NULL || new_path == NULL, EINVAL)
    _standard_assign(const auto err, rename(old_path, new_path))
    _ret_error_helper(err == -1, errno)
    _ret_finish_helper()
}

int fs_mkdir(char const *restrict path, const mode_t mode) {
    _ret_error_helper(path == NULL, EINVAL)
    _ret_error_helper(mode > 0777, EINVAL)
    _standard_assign(const auto err, mkdir(path, mode))
    _ret_error_helper(err == -1, errno)
    _ret_finish_helper()
}

int fs_rmdir(char const *restrict path) {
    _ret_error_helper(path == NULL, EINVAL)
    _standard_assign(const auto err, rmdir(path))
    _ret_error_helper(err == -1, errno)
    _ret_finish_helper()
}

int fs_chmod(char const *restrict path, const mode_t mode) {
    _ret_error_helper(path == NULL, EINVAL)
    _ret_error_helper(mode > 0777, EINVAL)
    _standard_assign(const auto err, chmod(path, mode))
    _ret_error_helper(err == -1, errno)
    _ret_finish_helper()
}

int fs_symlink_target(char const *restrict path, char *restrict buffer) {
    _ret_error_helper(path == NULL || buffer == NULL, EINVAL)
    _struct_builder(stat, st)
    _standard_assign(const auto err, lstat(path, &st))
    _ret_error_helper(err == -1, errno)
    _ret_error_helper(!S_ISLNK(st.st_mode), EINVAL)
    _standard_assign(const auto err2, fs_readlink(path, buffer, SPPC_PATH_MAX));
    _ret_error_helper(err2 == -1, errno)
    _ret_finish_helper();
}

int fs_stat(char const *restrict path, fd_stat_t *restrict out, const bool follow_symlink) {
    _ret_error_helper(path == NULL || out == NULL, EINVAL)
    _struct_builder(stat, st)
    _standard_assign(const auto err, (follow_symlink ? stat : lstat)(path, &st))
    _ret_error_helper(err == -1, errno)
    _standard_assign(*out, (fd_stat_t){
        .size = st.st_size,
        .blocks = st.st_blocks,
        .block_size = st.st_blksize,
        .mode = st.st_mode,
        .uid = st.st_uid,
        .gid = st.st_gid,
        .atime = st.st_atime,
        .mtime = st.st_mtime,
        .ctime = st.st_ctime
    })
    _ret_finish_helper();
}

int fs_chown(char const *restrict path, const uid_t owner, const gid_t group, const bool follow_symlink) {
    _ret_error_helper(path == NULL, EINVAL)
    _standard_assign(const auto err, (follow_symlink ? chown : lchown)(path, owner, group))
    _ret_error_helper(err == -1, errno)
    _ret_finish_helper();
}

int fs_access(char const *restrict path, const int flags) {
    _ret_error_helper(path == NULL, EINVAL)
    _ret_error_helper(flags & ~(F_OK | R_OK | W_OK | X_OK), EINVAL)
    _standard_assign(const auto err, access(path, flags))
    _ret_error_helper(err == -1, errno)
    _ret_finish_helper()
}

int fs_touch(char const *restrict path, const mode_t mode) {
    _ret_error_helper(path == NULL, EINVAL)
    _ret_error_helper(mode > 0777, EINVAL)
    _standard_assign(const auto err, utimensat(AT_FDCWD, path, NULL, 0))
    _ret_error_helper(err == -1 && errno != ENOENT, errno)
    _ret_finish_helper()
}

int fs_realpath(char const *restrict path, char *restrict buffer) {
    _ret_error_helper(path == NULL || buffer == NULL, EINVAL)
    _standard_assign(const auto err, realpath(path, buffer))
    _ret_error_helper(err == NULL, errno)
    _ret_finish_helper()
}

int fs_hardlink(char const *restrict target, char const *restrict linkpath) {
    _ret_error_helper(target == NULL || linkpath == NULL, EINVAL)
    _standard_assign(const auto err, link(target, linkpath))
    _ret_error_helper(err == -1, errno)
    _ret_finish_helper()
}

int fs_symlink(char const *restrict target, char const *restrict linkpath) {
    _ret_error_helper(target == NULL || linkpath == NULL, EINVAL)
    _standard_assign(const auto err, symlink(target, linkpath))
    _ret_error_helper(err == -1, errno)
    _ret_finish_helper()
}

int fs_readlink(char const *restrict path, char *restrict buffer, const size_t buffer_size) {
    _ret_error_helper(path == NULL || buffer == NULL, EINVAL)
    _ret_error_helper(buffer_size == 0, EINVAL)
    _eintr_repeat_assign(ssize_t, n, readlink(path, buffer, buffer_size - 1))
    _ret_error_helper(n == -1, errno)
    buffer[n] = '\0';
    _ret_finish_helper();
}

int fs_mktemp(char const *restrict path, fs_temp_t *restrict out) {
    _ret_error_helper(out == NULL, EINVAL)
    _standard_assign(const auto base, path != NULL ? path : "/tmp")
    _standard_assign(const auto suffix, "/tmp_XXXXXX")
    _ret_error_helper(strlen(base) + strlen(suffix) >= SPPC_PATH_MAX, ENAMETOOLONG)
    _standard_assign(char template_path[SPPC_PATH_MAX], {0})
    snprintf(template_path, sizeof(template_path), "%s%s", base, suffix);
    _eintr_repeat_assign(int, fd, mkostemp(template_path, O_CLOEXEC))
    _ret_error_helper(fd == -1, errno)
    _standard_assign(*out, (fs_temp_t){.fd = fd})
    strncpy(out->path, template_path, sizeof(out->path) - 1);
    out->path[sizeof(out->path) - 1] = '\0';
    _ret_finish_helper();
}

int fs_mktemp_dir(char const *restrict path, fs_temp_t *restrict out) {
    _ret_error_helper(out == NULL, EINVAL)
    _standard_assign(const auto base, path != NULL ? path : "/tmp")
    _standard_assign(const auto suffix, "/tmp_XXXXXX")
    _ret_error_helper(strlen(base) + strlen(suffix) >= SPPC_PATH_MAX, ENAMETOOLONG)
    _standard_assign(char template_path[SPPC_PATH_MAX], {0})
    snprintf(template_path, sizeof(template_path), "%s%s", base, suffix);
    _standard_assign(char const *err, mkdtemp(template_path))
    _ret_error_helper(err == NULL, errno)
    _standard_assign(*out, (fs_temp_t){.fd = -1})
    strncpy(out->path, template_path, sizeof(out->path) - 1);
    out->path[sizeof(out->path) - 1] = '\0';
    _ret_finish_helper();
}

int fs_statvfs(char const *restrict path, fs_statvfs_t *restrict out) {
    _ret_error_helper(path == NULL || out == NULL, EINVAL)
    _struct_builder(statvfs, raw)
    _standard_assign(const auto err, statvfs(path, &raw))
    _ret_error_helper(err == -1, errno)
    _standard_assign(*out, (fs_statvfs_t){
        .total_bytes = raw.f_blocks * raw.f_frsize,
        .free_bytes = raw.f_bfree * raw.f_frsize,
        .available_bytes = raw.f_bavail * raw.f_frsize,
        .total_inodes = raw.f_files,
        .free_inodes = raw.f_ffree,
        .block_size = raw.f_frsize,
        .max_filename_len = raw.f_namemax
    });
    _ret_finish_helper();
}

void* mm_malloc(const size_t size) {
    _ret_error_helper(size == 0, NULL)
    _standard_assign(const auto ptr, malloc(size))
    _ret_error_helper(ptr == NULL, NULL)
    return ptr;
}

void* mm_alloc_aligned(const size_t size, const size_t alignment) {
    _ret_error_helper(size == 0 || alignment == 0, NULL)
    _ret_error_helper(alignment & alignment - 1, NULL) // Must be power of two
    _standard_assign(const auto ptr, aligned_alloc(alignment, size))
    _ret_error_helper(ptr == NULL, NULL)
    return ptr;
}

void* mm_calloc(const size_t num, const size_t size) {
    _ret_error_helper(num == 0 || size == 0, NULL)
    _ret_error_helper(num > SIZE_MAX / size, NULL)
    _standard_assign(const auto ptr, calloc(num, size))
    _ret_error_helper(ptr == NULL, NULL)
    return ptr;
}

void* mm_realloc(void *ptr, const size_t new_size) {
    _ret_error_helper(new_size == 0, NULL)
    _standard_assign(const auto new_ptr, realloc(ptr, new_size))
    _ret_error_helper(new_ptr == NULL, NULL)
    return new_ptr;
}

void mm_free(void *ptr) {
    _ret_error_helper(ptr == NULL, )
    free(ptr);
}

int mm_mem_copy(void *restrict dest, void const *restrict src, const size_t size) {
    _ret_error_helper(dest == NULL || src == NULL, EINVAL)
    _ret_error_helper(size == 0, 0)
    _standard_assign(const auto err, memcpy(dest, src, size))
    _ret_error_helper(err == NULL, errno)
    _ret_finish_helper();
}

int mm_mem_move(void *restrict dest, void const *restrict src, const size_t size) {
    _ret_error_helper(dest == NULL || src == NULL, EINVAL)
    _ret_error_helper(size == 0, 0)
    _standard_assign(const auto err, memmove(dest, src, size))
    _ret_error_helper(err == NULL, errno)
    _ret_finish_helper();
}

int mm_mem_set(void *dest, const int value, const size_t size) {
    _ret_error_helper(dest == NULL, EINVAL)
    _ret_error_helper(size == 0, 0)
    _standard_assign(const auto err, memset(dest, value, size))
    _ret_error_helper(err == NULL, errno)
    _ret_finish_helper();
}

int mm_mem_zero(void *dest, const size_t size) {
    _ret_error_helper(dest == NULL, EINVAL)
    _ret_error_helper(size == 0, 0)
    _standard_assign(const auto err, memset(dest, 0, size))
    _ret_error_helper(err == NULL, errno)
    _ret_finish_helper();
}

int mm_mem_cmp(void const *ptr1, void const *ptr2, const size_t size, int *restrict out) {
    _ret_error_helper(ptr1 == NULL || ptr2 == NULL || out == NULL, EINVAL)
    _ret_error_helper(size == 0, 0)
    _standard_assign(const auto err, memcmp(ptr1, ptr2, size))
    _ret_error_helper(err == -1, errno)
    _sret_finish_helper(err);
}

int mm_mem_cmp_const(void const *ptr1, void const *ptr2, const size_t size, int *restrict out) {
    _ret_error_helper(ptr1 == NULL || ptr2 == NULL || out == NULL, EINVAL)
    _ret_error_helper(size == 0, 0)
    _standard_assign(const volatile unsigned char *a, ptr1);
    _standard_assign(const volatile unsigned char* b, ptr2);
    unsigned char result = 0;
    for (size_t i = 0; i < size; i++) { result |= a[i] ^ b[i]; }
    *out = result;
    return 0;
}

int mm_mem_find(void const *haystack, const size_t haystack_size, void const *needle, const size_t needle_size, size_t *restrict out) {
    _ret_error_helper(haystack == NULL || needle == NULL || out == NULL, EINVAL)
    _ret_error_helper(haystack_size == 0, 0)
    _ret_error_helper(needle_size == 0, *out = 0; return 0)
    _ret_error_helper(haystack_size < needle_size, *out = -1; return 0)
    _standard_assign(const auto err, memmem(haystack, haystack_size, needle, needle_size))
    _ret_error_helper(err == NULL, errno)
    _standard_assign(*out, err - haystack)
    _ret_finish_helper();
}

void* mm_mem_map(const size_t size) {
    _ret_error_helper(size == 0, NULL)
    _standard_assign(const auto err, mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0))
    _ret_error_helper(err == MAP_FAILED, NULL)
    return err;
}

int mm_mem_unmap(void *addr, const size_t size) {
    _ret_error_helper(addr == NULL, EINVAL)
    _ret_error_helper(size == 0, 0)
    _standard_assign(const auto err, munmap(addr, size))
    _ret_error_helper(err == -1, errno)
    _ret_finish_helper();
}

int mm_mem_lock(const void *addr, const size_t size) {
    _ret_error_helper(addr == NULL, EINVAL)
    _ret_error_helper(size == 0, 0)
    _standard_assign(const auto err, mlock(addr, size))
    _ret_error_helper(err == -1, errno)
    _ret_finish_helper();
}

int mm_mem_unlock(const void *addr, const size_t size) {
    _ret_error_helper(addr == NULL, EINVAL)
    _ret_error_helper(size == 0, 0)
    _standard_assign(const auto err, munlock(addr, size))
    _ret_error_helper(err == -1, errno)
    _ret_finish_helper();
}

int mm_mem_protect(void *addr, const size_t size, const int prot) {
    _ret_error_helper(addr == NULL, EINVAL)
    _ret_error_helper(size == 0, 0)
    _ret_error_helper(prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC), EINVAL)
    _standard_assign(const auto err, mprotect(addr, size, prot))
    _ret_error_helper(err == -1, errno)
    _ret_finish_helper();
}

int st_str_len(char const *restrict str, const size_t max_len, size_t *restrict out) {
    _ret_error_helper(str == NULL || out == NULL, EINVAL)
    _ret_error_helper(max_len == 0, 0)
    _standard_assign(const auto err, strlen(str))
    _ret_error_helper(err == -1, errno)
    _standard_assign(*out, err)
    _ret_finish_helper();
}

int st_str_cpy(char *restrict dest, const size_t dest_size, char const *restrict src, size_t *restrict out) {
    _ret_error_helper(dest == NULL || src == NULL || out == NULL, EINVAL)
    _ret_error_helper(dest_size == 0, 0)
    _standard_assign(const auto err, strncpy(dest, src, dest_size - 1))
    _ret_error_helper(err == NULL, errno)
    _standard_assign(dest[dest_size - 1], '\0')
    _standard_assign(*out, strlen(dest))
    _ret_finish_helper();
}

int st_str_cat(char *restrict dest, const size_t dest_size, char const *restrict src, size_t *restrict out) {
    _ret_error_helper(dest == NULL || src == NULL || out == NULL, EINVAL)
    _standard_assign(const auto err, strcat(dest, src))
    _ret_error_helper(err == NULL, errno)
    _standard_assign(dest[dest_size - 1], '\0')
    _standard_assign(*out, strlen(dest))
    _ret_finish_helper();
}

int st_str_cmp(char const *str1, char const *str2, int *restrict out) {
    _ret_error_helper(str1 == NULL || str2 == NULL || out == NULL, EINVAL)
    _standard_assign(const auto err, strcmp(str1, str2))
    _ret_error_helper(err == -1, errno)
    _sret_finish_helper(err);
}

int st_str_case_cmp(char const *str1, char const *str2, int *restrict out) {
    _ret_error_helper(str1 == NULL || str2 == NULL || out == NULL, EINVAL)
    _standard_assign(const auto err, strcasecmp(str1, str2))
    _ret_error_helper(err == -1, errno)
    _sret_finish_helper(err);
}

int st_str_chr(char const *str, const char ch, int *restrict out) {
    _ret_error_helper(str == NULL, EINVAL)
    _standard_assign(const auto err, strchr(str, ch))
    _ret_error_helper(err == NULL, errno)
    _sret_finish_helper(err - str);
}

int st_str_rchr(char const *str, const char ch, int *restrict out) {
    _ret_error_helper(str == NULL, EINVAL)
    _standard_assign(const auto err, strrchr(str, ch))
    _ret_error_helper(err == NULL, errno)
    _sret_finish_helper(err - str);
}

int st_str_str(char const *haystack, char const *needle, int *restrict out) {
    _ret_error_helper(haystack == NULL || needle == NULL || out == NULL, EINVAL)
    _standard_assign(const auto err, strstr(haystack, needle))
    _ret_error_helper(err == NULL, errno)
    _sret_finish_helper(err - haystack);
}

int st_str_case_str(char const *haystack, char const *needle, int *restrict out) {
    _ret_error_helper(haystack == NULL || needle == NULL || out == NULL, EINVAL)
    _standard_assign(const auto err, strcasestr(haystack, needle))
    _ret_error_helper(err == NULL, errno)
    _sret_finish_helper(err - haystack);
}

int st_str_pbrk(char const *string, char const *accept, int *restrict out) {
    _ret_error_helper(string == NULL || accept == NULL || out == NULL, EINVAL)
    _standard_assign(const auto err, strpbrk(string, accept))
    _ret_error_helper(err == NULL, errno)
    _sret_finish_helper(err - string)
}

void* st_str_dup(char const *str) {
    _ret_error_helper(str == NULL, NULL)
    _standard_assign(const auto dup, strdup(str))
    _ret_error_helper(dup == NULL, NULL)
    return dup;
}

int so_socket(const int domain, const int type, const int protocol, int *restrict out) {
    _ret_error_helper(domain != AF_INET && domain != AF_INET6, EINVAL)
    _ret_error_helper(type != SOCK_STREAM && type != SOCK_DGRAM, EINVAL)
    _ret_error_helper(protocol != IPPROTO_TCP && protocol != IPPROTO_UDP && protocol != 0, EINVAL)
    _ret_error_helper(out == NULL, EINVAL)
    _standard_assign(const auto fd, socket(domain, type, protocol))
    _ret_error_helper(fd == -1, errno)
    _sret_finish_helper(fd);
}

int so_close(const int socket_fd) {
    _ret_error_helper(socket_fd < 0, EINVAL)
    _standard_assign(const auto err, close(socket_fd))
    _ret_error_helper(err == -1, errno)
    _ret_finish_helper();
}

int so_shutdown(const int socket_fd, const int how) {
    _ret_error_helper(socket_fd < 0, EINVAL)
    _ret_error_helper(how != SHUT_RD && how != SHUT_WR && how != SHUT_RDWR, EINVAL)
    _standard_assign(const auto err, shutdown(socket_fd, how))
    _ret_error_helper(err == -1, errno)
    _ret_finish_helper();
}

int so_connect(const int socket_fd, so_addr_t const *restrict addr) {
    _ret_error_helper(addr == NULL, EINVAL)
    _ret_error_helper(socket_fd < 0, EINVAL)
    _so_address_helper(SOCK_STREAM, IPPROTO_TCP, 0, connect(socket_fd, ai->ai_addr, ai->ai_addrlen) == 0)
}

int so_bind(const int socket_fd, so_addr_t const *restrict addr) {
    _ret_error_helper(addr == NULL, EINVAL)
    _ret_error_helper(socket_fd < 0, EINVAL)
    _so_address_helper(SOCK_STREAM, IPPROTO_TCP, 1, bind(socket_fd, ai->ai_addr, ai->ai_addrlen) == 0)
}

int so_listen(const int socket_fd, const int backlog) {
    _ret_error_helper(socket_fd < 0, EINVAL)
    _ret_error_helper(backlog < 0, EINVAL)
    _standard_assign(const auto err, listen(socket_fd, backlog))
    _ret_error_helper(err == -1, errno)
    _ret_finish_helper();
}

int so_accept(const int socket_fd, so_addr_t *restrict addr, int *restrict out) {
    _ret_error_helper(addr == NULL || out == NULL, EINVAL)
    _ret_error_helper(socket_fd < 0, EINVAL)
    _struct_builder(sockaddr_storage, peer)
    _standard_assign(socklen_t addr_len, sizeof(peer))
    _eintr_repeat_assign(int, fd, accept4(socket_fd, (struct sockaddr*)&peer, &addr_len, SOCK_CLOEXEC))
    _ret_error_helper(fd == -1, errno)
    _so_address_return_helper(peer, close(fd);)
    _sret_finish_helper(fd)
}

int so_send(const int socket_fd, char const *data, const size_t size, const int flags, ssize_t *restrict out) {
    _ret_error_helper(data == NULL || out == NULL, EINVAL)
    _ret_error_helper(socket_fd < 0 || size == 0, EINVAL)
    _eintr_repeat_assign(ssize_t, n, send(socket_fd, data, size, flags | MSG_NOSIGNAL))
    _ret_error_helper(n == -1, errno)
    _sret_finish_helper(n)
}

int so_recv(const int socket_fd, char *buffer, const size_t size, const int flags, ssize_t *restrict out) {
    _ret_error_helper(buffer == NULL || out == NULL, EINVAL)
    _ret_error_helper(socket_fd < 0 || size == 0, EINVAL)
    _eintr_repeat_assign(ssize_t, n, recv(socket_fd, buffer, size, flags))
    _ret_error_helper(n == -1, errno)
    _sret_finish_helper(n)
}

int so_sendto(const int socket_fd, char const *data, const size_t size, so_addr_t const *restrict addr, ssize_t *restrict out) {
    _ret_error_helper(data == NULL || out == NULL, EINVAL)
    _ret_error_helper(socket_fd < 0 || size == 0, EINVAL)
    _standard_assign(*out, 0) // TODO
    _so_address_helper(SOCK_DGRAM, IPPROTO_UDP, 0, sendto(socket_fd, data, size, MSG_NOSIGNAL, ai->ai_addr, ai->ai_addrlen) != -1)
}

int so_recvfrom(const int socket_fd, char *buffer, const size_t size, so_addr_t *restrict addr, ssize_t *restrict out) {
    _ret_error_helper(addr == NULL || buffer == NULL || out == NULL, EINVAL)
    _ret_error_helper(socket_fd < 0 || size == 0, EINVAL)
    _struct_builder(sockaddr_storage, peer)
    _standard_assign(socklen_t addr_len, sizeof(peer))
    _eintr_repeat_assign(ssize_t, n, recvfrom(socket_fd, buffer, size, 0, (struct sockaddr*)&peer, &addr_len))
    _ret_error_helper(n == -1, errno)
    _so_address_return_helper(peer,)
    _sret_finish_helper(n)
}

int so_get_error(const int socket_fd, int *restrict out) {
    _ret_error_helper(out == NULL, EINVAL)
    _ret_error_helper(socket_fd < 0, EINVAL)
    _standard_assign(auto error, 0)
    _standard_assign(socklen_t len, sizeof(error))
    _standard_assign(const auto err, getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &error, &len))
    _ret_error_helper(err == -1, errno)
    _sret_finish_helper(error)
}

int so_set_nonblocking(const int socket_fd, const bool non_blocking) {
    _ret_error_helper(socket_fd < 0, EINVAL)
    _standard_assign(const auto err, fcntl(socket_fd, F_GETFL, 0))
    _ret_error_helper(err == -1, errno)
    _standard_assign(const auto flags, non_blocking ? err | O_NONBLOCK : err & ~O_NONBLOCK);
    _standard_assign(const auto err2, fcntl(socket_fd, F_SETFL, flags))
    _ret_error_helper(err2 == -1, errno)
    _ret_finish_helper();
}

int so_set_recv_timeout(const int socket_fd, const int timeout_ms) {
    _ret_error_helper(socket_fd < 0 || timeout_ms < 0, EINVAL)
    _standard_assign(const struct timeval tv, { .tv_sec = timeout_ms / 1000, .tv_usec = timeout_ms % 1000 * 1000 });
    _standard_assign(const auto err, setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)))
    _ret_error_helper(err == -1, errno)
    _ret_finish_helper();
}

int so_set_send_timeout(const int socket_fd, const int timeout_ms) {
    _ret_error_helper(socket_fd < 0 || timeout_ms < 0, EINVAL)
    _standard_assign(const struct timeval tv, { .tv_sec = timeout_ms / 1000, .tv_usec = timeout_ms % 1000 * 1000 });
    _standard_assign(const auto err, setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)))
    _ret_error_helper(err == -1, errno)
    _ret_finish_helper();
}

int so_set_reuseaddr(const int socket_fd, const bool reuse) {
    _ret_error_helper(socket_fd < 0, EINVAL)
    _standard_assign(const auto v, reuse ? 1 : 0)
    _standard_assign(const auto err, setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &v, sizeof(v)))
    _ret_error_helper(err == -1, errno)
    _ret_finish_helper();
}

int so_set_keepalive(const int socket_fd, const bool keepalive) {
    _ret_error_helper(socket_fd < 0, EINVAL)
    _standard_assign(const auto v, keepalive ? 1 : 0)
    _standard_assign(const auto err, setsockopt(socket_fd, SOL_SOCKET, SO_KEEPALIVE, &v, sizeof(v)))
    _ret_error_helper(err == -1, errno)
    _ret_finish_helper();
}

int so_set_linger(const int socket_fd, const bool linger) {
    _ret_error_helper(socket_fd < 0, EINVAL)
    _standard_assign(const auto v, linger ? 1 : 0)
    _standard_assign(const auto err, setsockopt(socket_fd, SOL_SOCKET, SO_LINGER, &v, sizeof(v)))
    _ret_error_helper(err == -1, errno)
    _ret_finish_helper();
}

int so_set_nodelay(const int socket_fd, const bool nodelay) {
    _ret_error_helper(socket_fd < 0, EINVAL)
    _standard_assign(const auto v, nodelay ? 1 : 0)
    _standard_assign(const auto err, setsockopt(socket_fd, IPPROTO_TCP, TCP_NODELAY, &v, sizeof(v)))
    _ret_error_helper(err == -1, errno)
    _ret_finish_helper();
}

int so_getsockname(const int socket_fd, so_addr_t *restrict addr) {
    _ret_error_helper(addr == NULL, EINVAL)
    _ret_error_helper(socket_fd < 0, EINVAL)
    _struct_builder(sockaddr_storage, local)
    _standard_assign(socklen_t addr_len, sizeof(local))
    _standard_assign(const auto err, getsockname(socket_fd, (struct sockaddr*)&local, &addr_len))
    _ret_error_helper(err == -1, errno)
    _so_address_return_helper(local,)
    _ret_finish_helper();
}

int so_getpeername(const int socket_fd, so_addr_t *restrict addr) {
    _ret_error_helper(addr == NULL, EINVAL)
    _ret_error_helper(socket_fd < 0, EINVAL)
    _struct_builder(sockaddr_storage, peer)
    _standard_assign(socklen_t addr_len, sizeof(peer))
    _standard_assign(const auto err, getpeername(socket_fd, (struct sockaddr*)&peer, &addr_len))
    _ret_error_helper(err == -1, errno)
    _so_address_return_helper(peer,)
    _ret_finish_helper();
}

int ti_gettime(const clockid_t clock, ti_duration_t *restrict out) {
    _ret_error_helper(out == NULL, EINVAL)
    _ret_error_helper(clock < 0, EINVAL)
    _struct_builder(timespec, ts)
    _standard_assign(const auto err, clock_gettime(clock, &ts))
    _ret_error_helper(err == -1, errno)
    _ret_error_helper(ts.tv_sec < 0 || ts.tv_nsec < 0, EINVAL)
    _sret_finish_helper((ti_duration_t){ .seconds = ts.tv_sec, .nanoseconds = ts.tv_nsec })
}

int ti_getres(const clockid_t clock, ti_duration_t *restrict out) {
    _ret_error_helper(out == NULL, EINVAL)
    _ret_error_helper(clock < 0, EINVAL)
    _struct_builder(timespec, res)
    _standard_assign(const auto err, clock_getres(clock, &res))
    _ret_error_helper(err == -1, errno)
    _ret_error_helper(res.tv_sec < 0 || res.tv_nsec < 0, EINVAL)
    _sret_finish_helper((ti_duration_t){ .seconds = res.tv_sec, .nanoseconds = res.tv_nsec })
}

int ti_nanosleep(const clockid_t clock, ti_duration_t const *duration) {
    _ret_error_helper(duration == NULL, EINVAL)
    _ret_error_helper(clock < 0, EINVAL)
    _ret_error_helper(duration->seconds < 0 || duration->nanoseconds < 0, EINVAL)
    _ret_error_helper(duration->nanoseconds >= 1000000000, EINVAL)
    _struct_builder(timespec, req, .tv_sec = duration->seconds, .tv_nsec = duration->nanoseconds)
    _struct_builder(timespec, rem, .tv_sec = 0, .tv_nsec = 0)
    while (clock_nanosleep(clock, 0, &req, &rem) == -1) {
        _ret_error_helper(errno != EINTR, errno)
        req = rem;
    }
    _ret_finish_helper();
}

int ti_localtime(const ti_duration_t *restrict ts, ti_breakdown_t *restrict out) {
    _ret_error_helper(ts == NULL || out == NULL, EINVAL)
    _ret_error_helper(ts->seconds < 0 || ts->nanoseconds < 0, EINVAL)
    _ret_error_helper(ts->nanoseconds >= 1000000000, EINVAL)
    _standard_assign(const auto t, ts->seconds)
    _struct_builder(tm, local)
    _standard_assign(const auto err, localtime_r(&t, &local))
    _ret_error_helper(err == NULL, errno)
    _sret_finish_helper((ti_breakdown_t){
        .year = local.tm_year + 1900,
        .month = local.tm_mon + 1,
        .day = local.tm_mday,
        .hour = local.tm_hour,
        .minute = local.tm_min,
        .second = local.tm_sec,
        .weekday = local.tm_wday,
        .yearday = local.tm_yday + 1,
        .is_dst = local.tm_isdst > 0,
        .tz_offset = local.tm_gmtoff,
    })
}

int ti_mktime(const ti_breakdown_t *restrict bd, ti_duration_t *restrict out) {
    _ret_error_helper(bd == NULL || out == NULL, EINVAL)
    _ret_error_helper(bd->second < 0 || bd->minute < 0 || bd->hour < 0 || bd->day < 1 || bd->month < 1 || bd->year < 1900, EINVAL)
    _ret_error_helper(bd->month > 12 || bd->day > 31 || bd->hour > 23 || bd->minute > 59 || bd->second > 60, EINVAL)
    _struct_builder(tm, local,
        .tm_year = bd->year - 1900,
        .tm_mon = bd->month - 1,
        .tm_mday = bd->day,
        .tm_hour = bd->hour,
        .tm_min = bd->minute,
        .tm_sec = bd->second,
    )
    _standard_assign(const auto t, mktime(&local))
    _ret_error_helper(t == -1, errno)
    _sret_finish_helper((ti_duration_t){ .seconds = t, .nanoseconds = 0 })
}

int ti_local_tz_name(char *restrict out) {
    _ret_error_helper(out == NULL, EINVAL)
    _standard_assign(const auto t, time(NULL))
    _ret_error_helper(t == -1, errno)
    _struct_builder(tm, local)
    _standard_assign(const auto err, localtime_r(&t, &local))
    _ret_error_helper(err == NULL, errno)
    _standard_assign(const auto tz, local.tm_zone)
    _ret_error_helper(tz == NULL, EINVAL)
    _standard_assign(const size_t tz_len, strlen(tz))
    _ret_error_helper(tz_len >= 255, EINVAL)
    strncpy(out, tz, tz_len);
    out[tz_len] = '\0';
    _ret_finish_helper(); // TODO: Use sret helper?
}

pid_t pr_pid() {
    return getpid();
}

pid_t pr_ppid() {
    return getppid();
}

uid_t pr_get_uid() {
    return getuid();
}

int pr_set_uid(const uid_t uid) {
    return setuid(uid);
}

gid_t pr_get_gid() {
    return getgid();
}

int pr_set_gid(const gid_t gid) {
    return setgid(gid);
}

uid_t pr_get_euid() {
    return geteuid();
}

gid_t pr_get_egid() {
    return getegid();
}

int pr_getenv(char const *restrict key, char *restrict out) {
    _ret_error_helper(key == NULL || out == NULL, EINVAL)
    _standard_assign(const auto err, secure_getenv(key))
    _ret_error_helper(err == NULL, errno)
    _sret_finish_helper(*err)
}

int pr_setenv(char const *restrict key, char const *restrict val, const bool overwrite) {
    _ret_error_helper(key == NULL || val == NULL, EINVAL)
    _standard_assign(const auto err, setenv(key, val, overwrite ? 1 : 0))
    _ret_error_helper(err == -1, errno)
    _ret_finish_helper();
}

int pr_unsetenv(char const *restrict key) {
    _ret_error_helper(key == NULL, EINVAL)
    _standard_assign(const auto err, unsetenv(key))
    _ret_error_helper(err == -1, errno)
    _ret_finish_helper();
}

// int pr_envvars(pr_env_t *restrict env) {
//     if (env == NULL) { local_errno = EINVAL; return -1; }
//     size_t count = 0;
//     while (environ[count] != NULL) { count++; }
//     env->count = count;
//     env->vars = malloc(count * sizeof(pr_env_var_t));
//     if (env->vars == NULL) { local_errno = ENOMEM; return -1; }
//     for (size_t i = 0; i < count; i++) {
//         const auto eq_pos = strchr(environ[i], '=');
//         if (eq_pos == NULL) {
//             env->vars[i].key[0] = '\0';
//             env->vars[i].val[0] = '\0';
//             continue;
//         }
//         const auto key_len = eq_pos - environ[i];
//         const auto val_len = strlen(eq_pos + 1);
//         strncpy(env->vars[i].key, environ[i], key_len);
//         strncpy(env->vars[i].val, eq_pos + 1, val_len);
//         env->vars[i].key[key_len] = '\0';
//         env->vars[i].val[val_len] = '\0';
//     }
//     local_errno = 0;
//     return 0;
// }
//
// int pr_free_envvars(pr_env_t *env) {
//     if (env == NULL || env->vars == NULL) { local_errno = EINVAL; return -1; }
//     free(env->vars);
//     env->vars = NULL;
//     env->count = 0;
//     local_errno = 0;
//     return 0;
// }

pid_t pr_exec(char const *restrict path, char const *const *restrict argv, char const *const *restrict envp, const int stdin_fd, const int stdout_fd, const int stderr_fd) {
    _ret_error_helper(path == NULL || argv == NULL, EINVAL)
    _ret_error_helper(access(path, X_OK) == -1, errno)
    _standard_assign(const auto pid, fork())
    _ret_error_helper(pid == -1, errno)
    if (pid == 0) {
        _exit_when(stdin_fd != -1 && dup2(stdin_fd, STDIN_FILENO) == -1, 127);
        _exit_when(stdout_fd != -1 && dup2(stdout_fd, STDOUT_FILENO) == -1, 127);
        _exit_when(stderr_fd != -1 && dup2(stderr_fd, STDERR_FILENO) == -1, 127);
        execve(path, (char *const *)argv, envp ? (char**)envp : environ);
        _exit(127);
    }
    return pid;
}

int pr_waitpid(const pid_t pid, pr_wait_result_t *restrict result) {
    _ret_error_helper(result == NULL, EINVAL)
    _ret_error_helper(pid < 0, EINVAL)
    _standard_assign(int status, 0);
    _eintr_repeat_assign(int, err, waitpid(pid, &status, 0))
    _ret_error_helper(err == -1, errno)
    _standard_assign(*result, (pr_wait_result_t){
        .exited = WIFEXITED(status) ? 1 : 0,
        .signaled = WIFSIGNALED(status) ? 1 : 0,
        .stopped = WIFSTOPPED(status) ? 1 : 0,
        .exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1,
        .signal = WIFSIGNALED(status) ? WTERMSIG(status) : -1,
    })
    _ret_finish_helper();
}

int pr_waitpid_nowait(const pid_t pid, pr_wait_result_t *restrict result) {
    _ret_error_helper(result == NULL, EINVAL)
    _ret_error_helper(pid < 0, EINVAL)
    _standard_assign(int status, 0);
    _eintr_repeat_assign(int, err, waitpid(pid, &status, WNOHANG))
    _ret_error_helper(err == -1, errno)
    _standard_assign(*result, (pr_wait_result_t){
        .exited = WIFEXITED(status) ? 1 : 0,
        .signaled = WIFSIGNALED(status) ? 1 : 0,
        .stopped = WIFSTOPPED(status) ? 1 : 0,
        .exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1,
        .signal = WIFSIGNALED(status) ? WTERMSIG(status) : -1,
    })
    _ret_finish_helper();
}

int pr_signal(const pid_t pid, const int signal) {
    _ret_error_helper(pid < 0 || signal < 0, EINVAL)
    _standard_assign(const auto err, kill(pid, signal))
    _ret_error_helper(err == -1, errno)
    _ret_finish_helper();
}

int pr_is_running(const pid_t pid) {
    _ret_error_helper(pid < 0, EINVAL)
    _standard_assign(const auto err, kill(pid, 0))
    _ret_error_helper(err == -1, errno)
    _ret_finish_helper();
}

int pr_get_cwd(char *restrict out) {
    _ret_error_helper(out == NULL, EINVAL)
    _standard_assign(const auto cwd, getcwd(out, 256))
    _ret_error_helper(cwd == NULL, errno)
    _sret_finish_helper(*cwd);
}

int pr_set_cwd(char const *restrict path) {
    _ret_error_helper(path == NULL, EINVAL)
    _standard_assign(const auto err, chdir(path))
    _ret_error_helper(err == -1, errno)
    _ret_finish_helper();
}

void pr_exit(const int status) {
    _exit(status);
}

void pr_exit_clean(const int status) {
    exit(status);
}

void pr_abort() {
    abort();
}

int rn_csprng_bytes(void *restrict out, const size_t size) {
    _ret_error_helper(out == NULL, EINVAL)
    _ret_error_helper(size == 0, EINVAL)
    _standard_assign(const auto ptr, (uint8_t *) out)
    _standard_assign(off_t offset, 0)
    while (offset < size) {
        _standard_assign(const auto result, getrandom(ptr + offset, size - offset, 0))
        _ret_error_helper(result == -1 && errno != EINTR, errno)
        if (result != -1) { offset += result; }
    }
    _ret_finish_helper(); // TODO: sret helper?
}

int rn_csprng_u32(uint32_t *restrict out) {
    _ret_error_helper(out == NULL, EINVAL)
    _standard_assign(const auto err, rn_csprng_bytes(out, sizeof(*out)))
    _ret_error_helper(err == -1, errno)
    _ret_finish_helper();
}

int rn_csprng_u64(uint64_t *restrict out) {
    _ret_error_helper(out == NULL, EINVAL)
    _standard_assign(const auto err, rn_csprng_bytes(out, sizeof(*out)))
    _ret_error_helper(err == -1, errno)
    _ret_finish_helper();
}

int rn_csprng_range(const uint64_t min, const uint64_t max, uint64_t *restrict out) {
    _ret_error_helper(out == NULL, EINVAL)
    _ret_error_helper(min > max, EINVAL)
    _standard_assign(const auto range, max - min + 1)
    _standard_assign(const auto threshold, (UINT64_MAX - range + 1) % range)
    while (1) {
        _standard_assign(const auto err, rn_csprng_u64(out))
        _ret_error_helper(err == -1, errno)
        if (*out >= threshold) { *out = min + *out % range; _ret_finish_helper(); }
    }
}

int rn_prng_seed(const uint64_t seed) {
    auto s = seed;
    for (size_t i = 0; i < 10; i++) {
        s += 0x9e3779b97f4a7c15ULL;
        auto z = s;
        z = (z ^ z >> 30) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ z >> 27) * 0x94d049bb133111ebULL;
        SPPC_PRNG_STATE.s[i] = z ^ z >> 31;
    }
    SPPC_PRNG_SEEDED = true;
    return 0;
}

int rn_prng_reset(void) {
    SPPC_PRNG_SEEDED = false;
    return _prng_ensure_seeded();
}

int rn_prng_bytes(void *restrict out, const size_t size) {
    _ret_error_helper(out == NULL, EINVAL)
    _ret_error_helper(size == 0, EINVAL)
    _ret_error_helper(_prng_ensure_seeded() == -1, errno)
    _standard_assign(const auto ptr, (uint8_t *) out)
    for (size_t i = 0; i < size; i++) {
        ptr[i] = (uint8_t)_xoshiro256ss_next(SPPC_PRNG_STATE.s);
    }
    _ret_finish_helper();
}

int rn_prng_u32(uint32_t *restrict out) {
    _ret_error_helper(out == NULL, EINVAL)
    _ret_error_helper(_prng_ensure_seeded() == -1, errno)
    _sret_finish_helper((uint32_t)_xoshiro256ss_next(SPPC_PRNG_STATE.s));
}

int rn_prng_u64(uint64_t *restrict out) {
    _ret_error_helper(out == NULL, EINVAL)
    _ret_error_helper(_prng_ensure_seeded() == -1, errno)
    _sret_finish_helper(_xoshiro256ss_next(SPPC_PRNG_STATE.s));
}

int rn_prng_double(double *restrict out) {
    _ret_error_helper(out == NULL, EINVAL)
    _ret_error_helper(_prng_ensure_seeded() == -1, errno)
    _standard_assign(const auto random_u64, _xoshiro256ss_next(SPPC_PRNG_STATE.s))
    _sret_finish_helper((random_u64 >> 11) * (1.0 / (UINT64_C(1) << 53)))
}

int rn_prng_next_range(const uint64_t min, const uint64_t max, uint64_t *restrict out) {
    _ret_error_helper(out == NULL, EINVAL)
    _ret_error_helper(min > max, EINVAL)
    _standard_assign(const auto range, max - min + 1)
    _standard_assign(const auto threshold, (UINT64_MAX - range + 1) % range)
    while (1) {
        _standard_assign(const auto val, _xoshiro256ss_next(SPPC_PRNG_STATE.s))
        if (val >= threshold) { *out = min + val % range; _ret_finish_helper(); }
    }
}

// int rn_prng_jump(void) {
//     if (_prng_ensure_seeded() == -1) { return -1; }
//
//     static constexpr uint64_t JUMP[] = {
//         0x180ec6d33cfd0abaULL, 0xd5a61266f0c9392cULL,
//         0xa9582618e03fc9aaULL, 0x39abdc4529b1661cULL
//     };
//
//     int64_t s0 = 0, s1 = 0, s2 = 0, s3 = 0;
//     for (auto i = 0; i < 4; i++) {
//         for (auto b = 0; b < 64; b++) {
//             if (JUMP[i] & UINT64_C(1) << b) {
//                 s0 ^= SPPC_PRNG_STATE.s[0];
//                 s1 ^= SPPC_PRNG_STATE.s[1];
//                 s2 ^= SPPC_PRNG_STATE.s[2];
//                 s3 ^= SPPC_PRNG_STATE.s[3];
//             }
//             _xoshiro256ss_next(SPPC_PRNG_STATE.s);
//         }
//     }
//
//     SPPC_PRNG_STATE.s[0] = s0; SPPC_PRNG_STATE.s[1] = s1;
//     SPPC_PRNG_STATE.s[2] = s2; SPPC_PRNG_STATE.s[3] = s3;
//     local_errno = 0;
//     return 0;
// }

int sys_sysconf(const int what, int64_t *restrict out) {
    _ret_error_helper(out == NULL, EINVAL)
    _ret_error_helper(what == -1, EINVAL)
    _standard_assign(const auto n, sysconf(what))
    _ret_error_helper(n == -1, errno)
    _sret_finish_helper(n);
}

int sys_gettid(uint64_t *restrict out) {
    _ret_error_helper(out == NULL, EINVAL)
    _standard_assign(const auto err, gettid())
    _ret_error_helper(err == -1, errno)
    _sret_finish_helper(gettid())
}

int sys_info(sys_info_t *restrict out) {
    _ret_error_helper(out == NULL, EINVAL)
    _struct_builder(sysinfo, raw)
    _standard_assign(const auto err, sysinfo(&raw))
    _ret_error_helper(err == -1, errno)
    _sret_finish_helper((sys_info_t){
        .total = raw.totalram,
        .free = raw.freeram,
        .shared = raw.sharedram,
        .buffered = raw.bufferram,
        .swap_total = raw.totalswap,
        .swap_free = raw.freeswap,
        .uptime_seconds = raw.uptime,
        .procs = raw.procs,
    })
}

// int sys_loadavg(sys_loadavg_t *restrict out) {
//     if (out == NULL) { local_errno = EINVAL; return -1; }
//
//     struct sysinfo raw;
//     const auto result = sysinfo(&raw);
//     if (result == -1) { local_errno = errno; return -1; }
//
//     constexpr auto scale = (double)(1 << SI_LOAD_SHIFT);
//     *out = (sys_loadavg_t){
//         .one = (double)raw.loads[0] / scale,
//         .five = (double)raw.loads[1] / scale,
//         .ten = (double)raw.loads[2] / scale,
//     };
//
//     local_errno = 0;
//     return 0;
// }
//
// int sys_uname(sys_uname_t *restrict out) {
//     if (out == NULL) { local_errno = EINVAL; return -1; }
//     struct utsname raw;
//     const auto result = uname(&raw);
//     if (result == -1) { local_errno = errno; return -1; }
//
//     *out = (sys_uname_t){
//         .sysname = raw.sysname,
//         .nodename = raw.nodename,
//         .domainname = raw.domainname,
//         .release = raw.release,
//         .version = raw.version,
//         .machine = raw.machine,
//     };
//
//     local_errno = 0;
//     return 0;
// }

int sys_gethostname(char *restrict out) {
    _ret_error_helper(out == NULL, EINVAL)
    _standard_assign(const auto err, gethostname(out, 256))
    _ret_error_helper(err == -1, errno)
    _ret_finish_helper();
}

int sys_sethostname(char const *restrict name) {
    _ret_error_helper(name == NULL, EINVAL)
    _standard_assign(const auto err, sethostname(name, strlen(name)))
    _ret_error_helper(err == -1, errno)
    _ret_finish_helper();
}

// int sys_getrlimit(const int resource, sys_rlimit_t *restrict out) {
//     if (out == NULL) { local_errno = EINVAL; return -1; }
//     struct rlimit raw;
//     const auto result = getrlimit(resource, &raw);
//     if (result == -1) { local_errno = errno; return -1; }
//     *out = (sys_rlimit_t){
//         .soft = raw.rlim_cur == RLIM_INFINITY ? UINT64_MAX : raw.rlim_cur,
//         .hard = raw.rlim_max == RLIM_INFINITY ? UINT64_MAX : raw.rlim_max,
//     };
//
//     local_errno = 0;
//     return 0;
// }
//
// int sys_setrlimit(const int resource, sys_rlimit_t const *limit) {
//     if (limit == NULL) { local_errno = EINVAL; return -1; }
//     const struct rlimit raw = {
//         .rlim_cur = limit->soft == UINT64_MAX ? RLIM_INFINITY : limit->soft,
//         .rlim_max = limit->hard == UINT64_MAX ? RLIM_INFINITY : limit->hard,
//     };
//     const auto result = setrlimit(resource, &raw);
//     local_errno = result == -1 ? errno : 0;
//     return result;
// }
//
// int sys_cache_line_size(int64_t *restrict out) {
//     if (out == NULL) { local_errno = EINVAL; return -1; }
//     const auto result = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
//     *out = (result == -1 || result == 0) ? 64 : result;
//     local_errno = 0;
//     return 0;
// }

int pt_thread_spawn(void*(*start_routine)(), uint64_t *restrict out) {
    _ret_error_helper(start_routine == NULL || out == NULL, EINVAL)
    _standard_assign(const auto err, pthread_create((pthread_t*)out, NULL, (void*(*)(void*))start_routine, NULL))
    _ret_error_helper(err != 0, errno)
    _ret_finish_helper();
}

int pt_thread_join(const uint64_t handle) {
    _ret_error_helper(handle <= 0, EINVAL)
    _standard_assign(const auto err, pthread_join(handle, NULL))
    _ret_error_helper(err != 0, errno)
    _ret_finish_helper();
}

int pt_thread_detach(const uint64_t handle) {
    _ret_error_helper(handle <= 0, EINVAL)
    _standard_assign(const auto err, pthread_detach(handle))
    _ret_error_helper(err != 0, errno)
    _ret_finish_helper();
}

int pt_thread_signal(const uint64_t handle, const int sig) {
    _ret_error_helper(handle <= 0 || sig < 0, EINVAL)
    _standard_assign(const auto err, pthread_kill(handle, sig))
    _ret_error_helper(err != 0, errno)
    _ret_finish_helper();
}

int pt_thread_cancel(const uint64_t handle) {
    _ret_error_helper(handle <= 0, EINVAL)
    _standard_assign(const auto err, pthread_cancel(handle))
    _ret_error_helper(err != 0, errno)
    _ret_finish_helper();
}

int pt_thread_self(uint64_t *restrict out) {
    _ret_error_helper(out == NULL, EINVAL)
    _standard_assign(const auto n, pthread_self())
    _sret_finish_helper(n);
}

int pt_thread_equal(const uint64_t handle1, const uint64_t handle2) {
    _ret_error_helper(handle1 <= 0 || handle2 <= 0, EINVAL)
    _standard_assign(const auto err, pthread_equal(handle1, handle2))
    _ret_error_helper(err != 0, errno)
    _ret_finish_helper();
}

int pt_thread_yield() {
    _standard_assign(const auto err, pthread_yield())
    _ret_error_helper(err != 0, errno)
    _ret_finish_helper();
}

int pt_mutex_create(const bool recursive, uint64_t *restrict out) {
    _ret_error_helper(out == NULL, EINVAL)
    _standard_assign(const auto m, (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t)))
    _ret_error_helper(m == NULL, ENOMEM)
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    _standard_assign(const auto err, pthread_mutex_init(m, &attr))
    pthread_mutexattr_destroy(&attr);
    _ret_error_helper(err != 0, errno)
    _sret_finish_helper((uintptr_t)m);
}

int pt_mutex_lock(const uint64_t handle) {
    _ret_error_helper(handle <= 0, EINVAL)
    _standard_assign(const auto err, pthread_mutex_lock((pthread_mutex_t*)handle))
    _ret_error_helper(err != 0, errno)
    _ret_finish_helper();
}

int pt_mutex_try_lock(const uint64_t handle) {
    _ret_error_helper(handle <= 0, EINVAL)
    _standard_assign(const auto err, pthread_mutex_trylock((pthread_mutex_t*)handle))
    _ret_error_helper(err == EBUSY, 1)
    _ret_error_helper(err != 0, errno)
    _ret_finish_helper();
}

int pt_mutex_timeout_lock(const uint64_t handle, ti_duration_t const *timeout) {
    _ret_error_helper(timeout == NULL, EINVAL)
    _ret_error_helper(handle <= 0, EINVAL)
    _ret_error_helper(timeout->seconds < 0 || timeout->nanoseconds < 0, EINVAL)
    _ret_error_helper(timeout->nanoseconds >= 1000000000, EINVAL)
    _pt_timeout_helper(mutex, lock)
}

int pt_mutex_unlock(const uint64_t handle) {
    _ret_error_helper(handle <= 0, EINVAL)
    _standard_assign(const auto err, pthread_mutex_unlock((pthread_mutex_t*)handle))
    _ret_error_helper(err != 0, errno)
    _ret_finish_helper();
}

int pt_mutex_destroy(const uint64_t handle) {
    _ret_error_helper(handle == 0, EINVAL)
    _standard_assign(const auto err, pthread_mutex_destroy((pthread_mutex_t*)handle))
    _ret_error_helper(err != 0, errno)
    free((pthread_mutex_t*)(uintptr_t)handle);
    _ret_finish_helper();
}

int pt_condvar_create(uint64_t *restrict out) {
    _ret_error_helper(out == NULL, EINVAL)
    _standard_assign(const auto m, (pthread_cond_t*)malloc(sizeof(pthread_cond_t)))
    _ret_error_helper(m == NULL, ENOMEM)
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setpshared(&attr, CLOCK_MONOTONIC);
    _standard_assign(const auto err, pthread_cond_init(m, &attr))
    _ret_error_helper(err != 0, errno)
    _ret_finish_helper();
}

int pt_condvar_wait(const uint64_t handle, const uint64_t mutex) {
    if (handle == 0 || mutex == 0) { local_errno = EINVAL; return -1; }
    const auto err = pthread_cond_wait((pthread_cond_t*)(uintptr_t)handle, (pthread_mutex_t*)(uintptr_t)mutex);
    local_errno = err == -1 ? err : 0;
    return err;
}

int pt_condvar_wait_timeout(const uint64_t handle, const uint64_t mutex, ti_duration_t const *timeout) {
    if (handle == 0 || mutex == 0 || timeout == NULL) { local_errno = EINVAL; return -1; }
    if (timeout->seconds < 0 || timeout->nanoseconds < 0) { local_errno = EINVAL; return -1; }
    if (timeout->nanoseconds >= 1000000000) { local_errno = EINVAL; return -1; }
    _pt_timeout_helper(cond, wait, (pthread_mutex_t*)(uintptr_t)mutex);
    return -1;
}

int pt_condvar_signal(const uint64_t handle) {
    if (handle == 0) { local_errno = EINVAL; return -1; }
    const auto err = pthread_cond_signal((pthread_cond_t*)(uintptr_t)handle);
    local_errno = err == -1 ? err : 0;
    return err != 0 ? -1 : 0;
}

int pt_condvar_broadcast(const uint64_t handle) {
    if (handle == 0) { local_errno = EINVAL; return -1; }
    const auto err = pthread_cond_broadcast((pthread_cond_t*)(uintptr_t)handle);
    local_errno = err == -1 ? err : 0;
    return err != 0 ? -1 : 0;
}

int pt_condvar_destroy(const uint64_t handle) {
    if (handle == 0) { local_errno = EINVAL; return -1; }
    const auto c = (pthread_cond_t*)(uintptr_t)handle;
    const auto err = pthread_cond_destroy(c);
    if (err != 0) { local_errno = err; return -1; }
    free(c);
    local_errno = 0;
    return 0;
}

int pt_rwlock_create(uint64_t *restrict out) {
    if (out == NULL) { local_errno = EINVAL; return -1; }
    const auto r = (pthread_rwlock_t*)malloc(sizeof(pthread_rwlock_t));
    if (r == NULL) { local_errno = ENOMEM; return -1; }
    const auto err = pthread_rwlock_init(r, NULL);
    if (err != 0) { free(r); local_errno = err; return -1; }
    *out = (uintptr_t)r;
    local_errno = 0;
    return 0;
}

int pt_rwlock_read_lock(const uint64_t handle) {
    if (handle == 0) { local_errno = EINVAL; return -1; }
    const auto err = pthread_rwlock_rdlock((pthread_rwlock_t*)(uintptr_t)handle);
    local_errno = err == -1 ? err : 0;
    return err != 0 ? -1 : 0;
}

int pt_rwlock_write_lock(const uint64_t handle) {
    if (handle == 0) { local_errno = EINVAL; return -1; }
    const auto err = pthread_rwlock_wrlock((pthread_rwlock_t*)(uintptr_t)handle);
    local_errno = err == -1 ? err : 0;
    return err != 0 ? -1 : 0;
}

int pt_rwlock_try_read_lock(const uint64_t handle) {
    if (handle == 0) { local_errno = EINVAL; return -1; }
    const auto err = pthread_rwlock_tryrdlock((pthread_rwlock_t*)(uintptr_t)handle);
    if (err == 0) { local_errno = 0; return 1; }
    if (err == EBUSY) { local_errno = 0; return 0; }
    local_errno = err == -1 ? err : 0;
    return -1;
}

int pt_rwlock_try_write_lock(const uint64_t handle) {
    if (handle == 0) { local_errno = EINVAL; return -1; }
    const auto err = pthread_rwlock_trywrlock((pthread_rwlock_t*)(uintptr_t)handle);
    if (err == 0) { local_errno = 0; return 1; }
    if (err == EBUSY) { local_errno = 0; return 0; }
    local_errno = err == -1 ? err : 0;
    return -1;
}

int pt_rwlock_read_timeout_lock(const uint64_t handle, const ti_duration_t *timeout) {
    if (handle == 0 || timeout == NULL) { local_errno = EINVAL; return -1; }
    if (timeout->seconds < 0 || timeout->nanoseconds < 0) { local_errno = EINVAL; return -1; }
    if (timeout->nanoseconds >= 1000000000) { local_errno = EINVAL; return -1; }
    _pt_timeout_helper(rwlock, rdlock);
    return -1;
}

int pt_rwlock_write_timeout_lock(const uint64_t handle, const ti_duration_t *timeout) {
    if (handle == 0 || timeout == NULL) { local_errno = EINVAL; return -1; }
    if (timeout->seconds < 0 || timeout->nanoseconds < 0) { local_errno = EINVAL; return -1; }
    if (timeout->nanoseconds >= 1000000000) { local_errno = EINVAL; return -1; }
    _pt_timeout_helper(rwlock, wrlock);
    return -1;
}

int pt_rwlock_unlock(const uint64_t handle) {
    if (handle == 0) { local_errno = EINVAL; return -1; }
    const auto err = pthread_rwlock_unlock((pthread_rwlock_t*)(uintptr_t)handle);
    local_errno = err != 0 ? -1 : 0;
    return err != 0 ? -1 : 0;
}

int pt_rwlock_destroy(const uint64_t handle) {
    if (handle == 0) { local_errno = EINVAL; return -1; }
    const auto r = (pthread_rwlock_t*)(uintptr_t)handle;
    const auto err = pthread_rwlock_destroy(r);
    if (err != 0) { local_errno = err; return -1; }
    free(r);
    local_errno = 0;
    return 0;
}

int pt_barrier_create(const uint32_t count, uint64_t *restrict out) {
    if (count == 0 || out == NULL) { local_errno = EINVAL; return -1; }
    const auto b = (pthread_barrier_t*)malloc(sizeof(pthread_barrier_t));
    if (b == NULL) { local_errno = ENOMEM; return -1; }
    const auto err = pthread_barrier_init(b, NULL, count);
    if (err != 0) { free(b); local_errno = err; return -1; }
    *out = (uintptr_t)b;
    local_errno = 0;
    return 0;
}

int pt_barrier_wait(const uint64_t handle) {
    if (handle == 0) { local_errno = EINVAL; return -1; }
    const auto err = pthread_barrier_wait((pthread_barrier_t*)(uintptr_t)handle);
    if (err == PTHREAD_BARRIER_SERIAL_THREAD) { local_errno = 0; return 1; }
    if (err == 0) { local_errno = 0; return 0; }
    local_errno = 0;
    return -1;
}

int pt_barrier_destroy(const uint64_t handle) {
    if (handle == 0) { local_errno = EINVAL; return -1; }
    const auto b = (pthread_barrier_t*)(uintptr_t)handle;
    const auto err = pthread_barrier_destroy(b);
    if (err != 0) { local_errno = err; return -1; }
    free(b);
    local_errno = 0;
    return 0;
}

int pt_spinlock_create(uint64_t *restrict out) {
    if (out == NULL) { local_errno = EINVAL; return -1; }
    const auto s = (pthread_spinlock_t*)malloc(sizeof(pthread_spinlock_t));
    if (s == NULL) { local_errno = ENOMEM; return -1; }
    const auto err = pthread_spin_init(s, PTHREAD_PROCESS_PRIVATE);
    if (err != 0) { free(s); local_errno = err; return -1; }
    *out = (uintptr_t)s;
    local_errno = 0;
    return 0;
}

int pt_spinlock_lock(const uint64_t handle) {
    if (handle == 0) { local_errno = EINVAL; return -1; }
    const auto err = pthread_spin_lock((pthread_spinlock_t*)(uintptr_t)handle);
    local_errno = err == -1 ? err : 0;
    return err != 0 ? -1 : 0;
}

int pt_spinlock_try_lock(const uint64_t spinlock_id) {
    if (spinlock_id == 0) { local_errno = EINVAL; return -1; }
    const auto err = pthread_spin_trylock((pthread_spinlock_t*)(uintptr_t)spinlock_id);
    if (err == 0) { local_errno = 0; return 1; }
    if (err == EBUSY) { local_errno = 0; return 0; }
    local_errno = err == -1 ? err : 0;
    return -1;
}

int pt_spinlock_unlock(const uint64_t spinlock_id) {
    if (spinlock_id == 0) { local_errno = EINVAL; return -1; }
    const auto err = pthread_spin_unlock((pthread_spinlock_t*)(uintptr_t)spinlock_id);
    local_errno = err == -1 ? err : 0;
    return err != 0 ? -1 : 0;
}

int pt_spinlock_destroy(const uint64_t spinlock_id) {
    if (spinlock_id == 0) { local_errno = EINVAL; return -1; }
    const auto s = (pthread_spinlock_t*)(uintptr_t)spinlock_id;
    const auto err = pthread_spin_destroy(s);
    if (err != 0) { local_errno = err; return -1; }
    free(s);
    local_errno = 0;
    return 0;
}
