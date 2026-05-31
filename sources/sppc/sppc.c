#define _GNU_SOURCE
#include <sppc/sppc.h>
#include <errno.h>        // error number definitions
#include <fcntl.h>        // file operations and macros
#include <poll.h>         // polling operations and macros
#include <libgen.h>       // path manipulation functions and macros
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

typedef struct {
    uint64_t s[4];
} rng_prng_t;

static constexpr auto SPPC_PATH_MAX = 4096;
static _Thread_local rng_prng_t SPPC_PRNG_STATE = {0};
static _Thread_local auto SPPC_PRNG_SEEDED = false;

static _Thread_local auto LOCAL_ERRNO = 0; // file descriptors
#define local_errno LOCAL_ERRNO

#define _so_address_helper(socktype, proto, bind, cond)              \
    char port_str[6];                                                \
    snprintf(port_str, sizeof(port_str), "%u", port);                \
                                                                     \
    struct addrinfo hints = {};                                      \
    memset(&hints, 0, sizeof(hints));                                \
    hints.ai_family = AF_UNSPEC;                                     \
    hints.ai_socktype = socktype;                                    \
    hints.ai_protocol = proto;                                       \
    if (bind) { hints.ai_flags = AI_PASSIVE; }                       \
                                                                     \
    struct addrinfo *res = NULL;                                     \
    const auto _gai_err = getaddrinfo(host, port_str, &hints, &res); \
    if (_gai_err != 0) { local_errno = _gai_err; return -1; }        \
                                                                     \
    auto _ret = -1;                                                  \
    for (auto ai = res; ai != NULL; ai = ai->ai_next) {              \
        if (cond) { _ret = 0; break; }                               \
    }                                                                \
                                                                     \
    local_errno = _ret == -1 ? errno : 0;                            \
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
            local_errno = EINVAL;                                               \
            return -1;                                                          \
        }                                                                       \
    }                                                                           \
    local_errno = 0;

#define _pt_timeout_helper(what, op, ...)                        \
    struct timespec abs_timeout;                                 \
    clock_gettime(CLOCK_REALTIME, &abs_timeout);                 \
    abs_timeout.tv_sec += timeout->seconds;                      \
    abs_timeout.tv_nsec += timeout->nanoseconds;                 \
    if (abs_timeout.tv_nsec >= 1000000000) {                     \
        abs_timeout.tv_sec += 1;                                 \
        abs_timeout.tv_nsec -= 1000000000;                       \
    }                                                            \
    const auto err = pthread_ ## what ## _timed ## op(           \
        (pthread_ ## what ## _t*)(uintptr_t)handle               \
        __VA_OPT__(, __VA_ARGS__), &abs_timeout);                \
    if (err == 0) { local_errno = 0; return 1; }                 \
    if (err == ETIMEDOUT) { local_errno = 0; return 0; }         \
    local_errno = err == -1 ? err : 0;

void _fill_stat(fd_stat_t *restrict dst, const struct stat *restrict src) {
    dst->size = src->st_size;
    dst->blocks = src->st_blocks;
    dst->block_size = src->st_blksize;
    dst->mode = src->st_mode;
    dst->uid = src->st_uid;
    dst->gid = src->st_gid;
    dst->atime = src->st_atime;
    dst->mtime = src->st_mtime;
    dst->ctime = src->st_ctime;
}

void _fill_statvfs(fs_statvfs_t *restrict dst, const struct statvfs *restrict src) {
    dst->total_bytes = src->f_blocks * src->f_frsize;
    dst->free_bytes = src->f_bfree * src->f_frsize;
    dst->avail_bytes = src->f_bavail * src->f_frsize;
    dst->total_inodes = src->f_files;
    dst->free_inodes = src->f_ffree;
    dst->block_size = src->f_frsize;
    dst->max_filename = src->f_namemax;
}

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

void init_c() {
    signal(SIGPIPE, SIG_IGN); // Ignore SIGPIPE to prevent crashes on broken pipes.
    _prng_ensure_seeded();
}

int get_errno() {
    return local_errno;
}

ssize_t fd_read(void *restrict buffer, const size_t size, const size_t count, const int fd) {
    if (buffer == NULL || size == 0 || count == 0 || fd < 0) { local_errno = EINVAL; return -1; }
    if (count > SIZE_MAX / size) { local_errno = EOVERFLOW; return -1; }
    ssize_t n; do { n = read(fd, buffer, size * count); } while (n == -1 && errno == EINTR);
    local_errno = n == -1 ? errno : 0;
    return n;
}

ssize_t fd_write(void const *restrict buffer, const size_t size, const size_t count, const int fd) {
    if (buffer == NULL || size == 0 || count == 0 || fd < 0) { local_errno = EINVAL; return -1; }
    if (count > SIZE_MAX / size) { local_errno = EOVERFLOW; return -1; }
    ssize_t n; do { n = write(fd, buffer, size * count); } while (n == -1 && errno == EINTR);
    local_errno = n == -1 ? errno : 0;
    return n;
}

int fd_open(char const *restrict filename, const int flags, const mode_t mode) {
    if (filename == NULL) { local_errno = EINVAL; return -1; }
    int fd; do { fd = open(filename, flags, mode); } while (fd == -1 && errno == EINTR);
    local_errno = fd == -1 ? errno : 0;
    return fd;
}

int fd_close(const int fd) {
    if (fd < 0) { local_errno = EINVAL; return -1; }
    const auto result = close(fd);
    if (result == -1 && errno == EINTR) { return 0; } // If interrupted, consider it closed.
    local_errno = result == -1 ? errno : 0;
    return result;
}

int fd_flush(const int fd) {
    if (fd < 0) { local_errno = EINVAL; return -1; }
    const auto result = fsync(fd);
    local_errno = result == -1 ? errno : 0;
    return result;
}

int fd_flush_data(const int fd) {
    if (fd < 0) { local_errno = EINVAL; return -1; }
    const auto result = fdatasync(fd);
    local_errno = result == -1 ? errno : 0;
    return result;
}

off_t fd_seek(const int fd, const off_t offset, const int whence) {
    if (fd < 0) { local_errno = EINVAL; return -1; }
    if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) { local_errno = EINVAL; return -1; }
    const auto result = lseek(fd, offset, whence);
    local_errno = result == -1 ? errno : 0;
    return result;
}

off_t fd_tell(const int fd) {
    if (fd < 0) { local_errno = EINVAL; return -1; }
    const auto result = lseek(fd, 0, SEEK_CUR);
    local_errno = result == -1 ? errno : 0;
    return result;
}

int fd_truncate(const int fd, const off_t length) {
    if (fd < 0 || length < 0) { local_errno = EINVAL; return -1; }
    const auto result = ftruncate(fd, length);
    local_errno = result == -1 ? errno : 0;
    return result;
}

int fd_lock_ex(const int fd, const bool non_blocking) {
    if (fd < 0) { local_errno = EINVAL; return -1; }
    struct flock fl = { .l_type = F_WRLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = 0 };
    const auto cmd = non_blocking ? F_SETLK : F_SETLKW;
    int result; do { result = fcntl(fd, cmd, &fl); } while (result == -1 && errno == EINTR && !non_blocking);
    local_errno = result == -1 ? errno : 0;
    return result;
}

int fd_lock_sh(const int fd, const bool non_blocking) {
    if (fd < 0) { local_errno = EINVAL; return -1; }
    struct flock fl = { .l_type = F_RDLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = 0 };
    const auto cmd = non_blocking ? F_SETLK : F_SETLKW;
    int result; do { result = fcntl(fd, cmd, &fl); } while (result == -1 && errno == EINTR && !non_blocking);
    local_errno = result == -1 ? errno : 0;
    return result;
}

int fd_unlock(const int fd) {
    if (fd < 0) { local_errno = EINVAL; return -1; }
    struct flock fl = { .l_type = F_UNLCK, .l_whence = SEEK_SET, .l_start = 0, .l_len = 0 };
    int result; do { result = fcntl(fd, F_SETLK, &fl); } while (result == -1 && errno == EINTR);
    local_errno = result == -1 ? errno : 0;
    return result;
}

int fd_stat(const int fd, fd_stat_t *restrict st) {
    if (fd < 0 || st == NULL) { local_errno = EINVAL; return -1; }
    struct stat raw;
    const auto result = fstat(fd, &raw);
    if (result == -1) { local_errno = errno; return -1; }
    _fill_stat(st, &raw);
    local_errno = 0;
    return 0;
}

int fd_stat_path(char const *restrict path, fd_stat_t *restrict st, const bool follow_symlink) {
    if (path == NULL || st == NULL) { local_errno = EINVAL; return -1; }
    struct stat raw;
    const auto result = (follow_symlink ? stat : lstat)(path, &raw);
    if (result == -1) { local_errno = errno; return -1; }
    _fill_stat(st, &raw);
    local_errno = 0;
    return 0;
}

int fd_dup(const int fd) {
    if (fd < 0) { local_errno = EINVAL; return -1; }
    const auto result = dup(fd);
    local_errno = result == -1 ? errno : 0;
    return result;
}

int fd_dup_into(const int fd, const int target_fd) {
    if (fd < 0 || target_fd < 0) { local_errno = EINVAL; return -1; }
    if (fd == target_fd) { local_errno = 0; return target_fd; }
    int result; do { result = dup2(fd, target_fd); } while (result == -1 && errno == EINTR);
    local_errno = result == -1 ? errno : 0;
    return result;
}

int fd_pipe(fd_pipe_t *restrict pipe_pair) {
    if (pipe_pair == NULL) { local_errno = EINVAL; return -1; }
    int fds[2];
#ifdef __linux__
    const auto result = pipe2(fds, O_CLOEXEC);
#else
    const auto result = pipe(fds);
    if (result == 0) {
        fcntl(fds[0], F_SETFD, FD_CLOEXEC);
        fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    }
#endif
    if (result == -1) { local_errno = errno; return -1; }
    pipe_pair->read_fd = fds[0];
    pipe_pair->write_fd = fds[1];
    local_errno = 0;
    return 0;
}

int fd_get_flags(const int fd) {
    if (fd < 0) { local_errno = EINVAL; return -1; }
    const auto result = fcntl(fd, F_GETFL);
    local_errno = result == -1 ? errno : 0;
    return result;
}

int fd_set_flags(const int fd, const int flags) {
    if (fd < 0) { local_errno = EINVAL; return -1; }
    const auto result = fcntl(fd, F_SETFL, flags);
    local_errno = result == -1 ? errno : 0;
    return result;
}

int fd_get_status(const int fd) {
    if (fd < 0) { local_errno = EINVAL; return -1; }
    const auto result = fcntl(fd, F_GETOWN);
    local_errno = result == -1 ? errno : 0;
    return result;
}

int fd_set_status(const int fd, const int flags) {
    if (fd < 0) { local_errno = EINVAL; return -1; }
    const auto result = fcntl(fd, F_SETOWN, flags);
    local_errno = result == -1 ? errno : 0;
    return result;
}

// MOVE TO S+++
// int fd_add_status(const int fd, const int flags) {
//     if (fd < 0) { local_errno = EINVAL; return -1; }
//     const auto current = fcntl(fd, F_GETOWN);
//     if (current == -1) { local_errno = errno; return -1; }
//     const auto result = fcntl(fd, F_SETOWN, current | flags);
//     local_errno = result == -1 ? errno : 0;
//     return result;
// }
//
// int fd_remove_status(const int fd, const int flags) {
//     if (fd < 0) { local_errno = EINVAL; return -1; }
//     const auto current = fcntl(fd, F_GETOWN);
//     if (current == -1) { local_errno = errno; return -1; }
//     const auto result = fcntl(fd, F_SETOWN, current & ~flags);
//     local_errno = result == -1 ? errno : 0;
//     return result;
// }

int fd_poll(fd_poll_t *restrict fds, const int count, const int timeout_ms) {
    if (fds == NULL || count <= 0 ) { local_errno = EINVAL; return -1; }
    int result; do { result = poll((struct pollfd*)fds, count, timeout_ms); } while (result == -1 && errno == EINTR);
    local_errno = result == -1 ? errno : 0;
    return result;
}

ssize_t fd_readv(const int fd, fd_iovec_t const *restrict iov, const int iov_count) {
    if (fd < 0 || iov == NULL || iov_count <= 0) { local_errno = EINVAL; return -1; }
    ssize_t result; do { result = readv(fd, (const struct iovec*)iov, iov_count); } while (result == -1 && errno == EINTR);
    local_errno = result == -1 ? errno : 0;
    return result;
}

ssize_t fd_writev(const int fd, fd_iovec_t const *restrict iov, const int iov_count) {
    if (fd < 0 || iov == NULL || iov_count <= 0) { local_errno = EINVAL; return -1; }
    ssize_t result; do { result = writev(fd, (const struct iovec*)iov, iov_count); } while (result == -1 && errno == EINTR);
    local_errno = result == -1 ? errno : 0;
    return result;
}

ssize_t fd_pread(const int fd, void *restrict buffer, const size_t size, const ssize_t count, const off_t offset) {
    if (fd < 0 || buffer == NULL || size == 0 || count == 0) { local_errno = EINVAL; return -1; }
    if (count > SIZE_MAX / size) { local_errno = EOVERFLOW; return -1; }
    ssize_t n; do { n = pread(fd, buffer, size * count, offset); } while (n == -1 && errno == EINTR);
    local_errno = n == -1 ? errno : 0;
    return n;
}

ssize_t fd_pwrite(const int fd, void const *restrict buffer, const size_t size, const ssize_t count, const off_t offset) {
    if (fd < 0 || buffer == NULL || size == 0 || count == 0) { local_errno = EINVAL; return -1; }
    if (count > SIZE_MAX / size) { local_errno = EOVERFLOW; return -1; }
    ssize_t n; do { n = pwrite(fd, buffer, size * count, offset); } while (n == -1 && errno == EINTR);
    local_errno = n == -1 ? errno : 0;
    return n;
}

int fd_mmap(const int fd, const size_t length, const int prot, const int flags, const off_t offset, fd_mmap_t *restrict out) {
    if (fd < 0 || length == 0 || out == NULL || offset < 0) { local_errno = EINVAL; return -1; }
    const auto addr = mmap(NULL, length, prot, flags, fd, offset);
    if (addr == MAP_FAILED) { local_errno = errno; return -1; }
    out->addr = addr;
    out->length = length;
    local_errno = 0;
    return 0;
}

int fd_munmap(fd_mmap_t *restrict mmap) {
    if (mmap == NULL || mmap->addr == NULL || mmap->length == 0) { local_errno = EINVAL; return -1; }
    const auto result = munmap(mmap->addr, mmap->length);
    if (result == 0) { mmap->addr = NULL; mmap->length = 0; }
    local_errno = result == -1 ? errno : 0;
    return result;
}

int fd_msync(fd_mmap_t const *restrict mmap, const size_t length, const int flags) {
    if (mmap == NULL || mmap->addr == NULL || length == 0) { local_errno = EINVAL; return -1; }
    const auto result = msync(mmap->addr, length, flags);
    local_errno = result == -1 ? errno : 0;
    return result;
}

int fd_madvise(fd_mmap_t const *restrict mmap, const size_t length, const int advice) {
    if (mmap == NULL || mmap->addr == NULL || length == 0) { local_errno = EINVAL; return -1; }
    const auto result = madvise(mmap->addr, length, advice);
    local_errno = result == -1 ? errno : 0;
    return result;
}

int fs_exists(char const *restrict path) {
    if (path == NULL) { local_errno = EINVAL; return -1; }
    struct stat st;
    if (lstat(path, &st) == 0) { local_errno = 0; return 1; }
    if (errno == ENOENT || errno == ENOTDIR) { local_errno = 0; return 0; }
    local_errno = errno;
    return -1;
}

int fs_is_file(char const *restrict path) {
    if (path == NULL) { local_errno = EINVAL; return -1; }
    struct stat st;
    if (lstat(path, &st) == -1) { local_errno = errno; return -1; }
    local_errno = 0;
    return S_ISREG(st.st_mode) ? 1 : 0;
}

int fs_is_dir(char const *restrict path) {
    if (path == NULL) { local_errno = EINVAL; return -1; }
    struct stat st;
    if (lstat(path, &st) != 0) { local_errno = errno; return -1; }
    local_errno = 0;
    return S_ISDIR(st.st_mode) ? 1 : 0;
}

int fs_is_symlink(char const *restrict path) {
    if (path == NULL) { local_errno = EINVAL; return -1; }
    struct stat st;
    if (lstat(path, &st) != 0) { local_errno = errno; return -1; }
    local_errno = 0;
    return S_ISLNK(st.st_mode) ? 1 : 0;
}

long long fs_file_size(char const *restrict path) {
    if (path == NULL) { local_errno = EINVAL; return -1; }
    struct stat st;
    if (lstat(path, &st) != 0) { local_errno = errno; return -1; }
    if (!S_ISREG(st.st_mode)) { local_errno = EINVAL; return -1; }
    local_errno = 0;
    return st.st_size;
}

int fs_remove(char const *restrict path) {
    if (path == NULL) { local_errno = EINVAL; return -1; }
    const auto result = remove(path);
    local_errno = result == -1 ? errno : 0;
    return result;
}

int fs_rename(char const *restrict old_path, char const *restrict new_path) {
    if (old_path == NULL || new_path == NULL) { local_errno = EINVAL; return -1; }
    const auto result = rename(old_path, new_path);
    local_errno = result == -1 ? errno : 0;
    return result;
}

int fs_mkdir(char const *restrict path, const mode_t mode) {
    if (path == NULL || mode > 0777) { local_errno = EINVAL; return -1; }
    const auto result = mkdir(path, mode);
    local_errno = result == -1 ? errno : 0;
    return result;
}

int fs_rmdir(char const *restrict path) {
    if (path == NULL) { local_errno = EINVAL; return -1; }
    const auto result = rmdir(path);
    local_errno = result == -1 ? errno : 0;
    return result;
}

int fs_chmod(char const *restrict path, const mode_t mode) {
    if (path == NULL || mode > 0777) { local_errno = EINVAL; return -1; }
    const auto result = chmod(path, mode);
    local_errno = result == -1 ? errno : 0;
    return result;
}

int fs_symlink_target(char const *restrict path, char **restrict buffer) {
    if (path == NULL || buffer == NULL) { local_errno = EINVAL; return -1; }
    const auto size = fs_file_size(path);
    if (size == -1) { return -1; }
    const auto buf = (char*)malloc(size + 1);
    if (buf == NULL) { local_errno = ENOMEM; return -1; }
    ssize_t n; do { n = readlink(path, buf, size); } while (n == -1 && errno == EINTR);
    if (n == -1) { local_errno = errno; free(buf); return -1; }
    buf[n] = '\0';
    *buffer = buf;
    local_errno = 0;
    return 0;
}

int fs_stat(char const *restrict path, fd_stat_t *restrict st) {
    if (path == NULL || st == NULL) { local_errno = EINVAL; return -1; }
    struct stat raw;
    if (stat(path, &raw) == -1) { local_errno = errno; return -1; }
    _fill_stat(st, &raw);
    local_errno = 0;
    return 0;
}

int fs_stat_link(char const *restrict path, fd_stat_t *restrict st) {
    if (path == NULL || st == NULL) { local_errno = EINVAL; return -1; }
    struct stat raw;
    if (lstat(path, &raw) == -1) { local_errno = errno; return -1; }
    _fill_stat(st, &raw);
    local_errno = 0;
    return 0;
}

int fs_chown(char const *restrict path, const uid_t owner, const gid_t group) {
    if (path == NULL || owner < -1 || group < -1) { local_errno = EINVAL; return -1; }
    const auto result = chown(path, owner, group);
    local_errno = result == -1 ? errno : 0;
    return result;
}

int fs_chown_link(char const *restrict path, const uid_t owner, const gid_t group) {
    if (path == NULL || owner < -1 || group < -1) { local_errno = EINVAL; return -1; }
    const auto result = lchown(path, owner, group);
    local_errno = result == -1 ? errno : 0;
    return result;
}

int fs_access(char const *restrict path, const int flags) {
    if (path == NULL) { local_errno = EINVAL; return -1; }
    if (flags & ~(F_OK | R_OK | W_OK | X_OK)) { local_errno = EINVAL; return -1; }
    const auto result = access(path, flags);
    local_errno = result == -1 ? errno : 0;
    return result;
}

int fs_touch(char const *restrict path, const mode_t mode) {
    if (path == NULL || mode > 0777) { local_errno = EINVAL; return -1; }
    if (utimensat(AT_FDCWD, path, NULL, 0) == 0) { local_errno = 0; return 0; }
    if (errno != ENOENT) { local_errno = errno; return -1; }
    int fd; do { fd = open(path, O_CREAT | O_WRONLY, mode); } while (fd == -1 && errno == EINTR);
    if (fd == -1) { local_errno = errno; return -1; }
    close(fd);
    local_errno = 0;
    return 0;
}

int fs_realpath(char const *restrict path, char *restrict buffer) {
    if (path == NULL || buffer == NULL) { local_errno = EINVAL; return -1; }
    const auto real = realpath(path, buffer);
    if (real == NULL) { local_errno = errno; return -1; }
    local_errno = 0;
    return 0;
}

int fs_hardlink(char const *restrict target, char const *restrict linkpath) {
    if (target == NULL || linkpath == NULL) { local_errno = EINVAL; return -1; }
    const auto result = link(target, linkpath);
    local_errno = result == -1 ? errno : 0;
    return result;
}

int fs_symlink(char const *restrict target, char const *restrict linkpath) {
    if (target == NULL || linkpath == NULL) { local_errno = EINVAL; return -1; }
    const auto result = symlink(target, linkpath);
    local_errno = result == -1 ? errno : 0;
    return result;
}

int fs_readlink(char const *restrict path, char *restrict buffer, const size_t buffer_size) {
    if (path == NULL || buffer == NULL || buffer_size == 0) { local_errno = EINVAL; return -1; }
    ssize_t n; do { n = readlink(path, buffer, buffer_size - 1); } while (n == -1 && errno == EINTR);
    if (n == -1) { local_errno = errno; return -1; }
    buffer[n] = '\0';
    local_errno = 0;
    return 0;
}

int fs_mktmp(char const *restrict path, fs_temp_t *restrict out) {
    // Temp file creation
    if (out == NULL) { local_errno = EINVAL; return -1; }
    const auto base = path != NULL ? path : "/tmp";
    const auto suffix = "/tmp_XXXXXX";
    if (strlen(base) + strlen(suffix) >= SPPC_PATH_MAX) { local_errno = ENAMETOOLONG; return -1; }

    char template_path[SPPC_PATH_MAX];
    snprintf(template_path, sizeof(template_path), "%s%s", base, suffix);

    int fd; do { fd = mkostemp(template_path, O_CLOEXEC); } while (fd == -1 && errno == EINTR);
    if (fd == -1) { local_errno = errno; return -1; }

    out->fd = fd;
    strncpy(out->path, template_path, sizeof(out->path) - 1);
    out->path[sizeof(out->path) - 1] = '\0'; // Ensure null-termination
    local_errno = 0;
    return 0;
}

int fs_mktmp_dir(char const *restrict path, fs_temp_t *restrict out) {
    // Temp directory creation
    if (out == NULL) { local_errno = EINVAL; return -1; }
    const auto base = path != NULL ? path : "/tmp";
    const auto suffix = "/tmpdir_XXXXXX";
    if (strlen(base) + strlen(suffix) >= SPPC_PATH_MAX) { local_errno = ENAMETOOLONG; return -1; }

    char template_path[SPPC_PATH_MAX];
    snprintf(template_path, sizeof(template_path), "%s%s", base, suffix);

    const auto result = mkdtemp(template_path);
    if (result == NULL) { local_errno = errno; return -1; }

    out->fd = -1; // No file descriptor for directories
    strncpy(out->path, template_path, sizeof(out->path) - 1);
    out->path[sizeof(out->path) - 1] = '\0'; // Ensure null-termination
    local_errno = 0;
    return 0;
}

int fs_statvfs(char const *restrict path, fs_statvfs_t *restrict st) {
    if (path == NULL || st == NULL) { local_errno = EINVAL; return -1; }
    struct statvfs raw;
    if (statvfs(path, &raw) == -1) { local_errno = errno; return -1; }
    _fill_statvfs(st, &raw);
    local_errno = 0;
    return 0;
}

void* mm_malloc(const size_t size) {
    if (size == 0) { local_errno = EINVAL; return NULL; }
    const auto ptr = malloc(size);
    local_errno = ptr == NULL ? ENOMEM : 0;
    return ptr;
}

void* mm_alloc_aligned(const size_t size, const size_t alignment) {
    if (size == 0 || alignment == 0) { local_errno = EINVAL; return NULL; }
    if (alignment & alignment - 1) { local_errno = EINVAL; return NULL; } // Must be power of two
    const auto ptr = aligned_alloc(alignment, size);
    local_errno = ptr == NULL ? errno : 0;
    return ptr;
}

void* mm_calloc(const size_t num, const size_t size) {
    if (num == 0 || size == 0) { local_errno = EINVAL; return NULL; }
    if (num > SIZE_MAX / size) { local_errno = EOVERFLOW; return NULL; }
    const auto ptr = calloc(num, size);
    local_errno = ptr == NULL ? ENOMEM : 0;
    return ptr;
}

void* mm_realloc(void *ptr, const size_t new_size) {
    if (new_size == 0) { local_errno = EINVAL; return NULL; }
    const auto new_ptr = realloc(ptr, new_size);
    local_errno = new_ptr == NULL ? ENOMEM : 0;
    return new_ptr;
}

void mm_free(void *ptr) {
    free(ptr);
    local_errno = 0;
}

void mm_mem_copy(void *restrict dst, void const *restrict src, const size_t size) {
    if (dst == NULL || src == NULL) { local_errno = EINVAL; return; }
    if (size == 0) { local_errno = 0; return; }
    const auto result =memcpy(dst, src, size);
    local_errno = result == NULL ? errno : 0;
}

void mm_mem_move(void *restrict dst, void const *restrict src, const size_t size) {
    if (dst == NULL || src == NULL) { local_errno = EINVAL; return; }
    if (size == 0) { local_errno = 0; return; }
    const auto result = memmove(dst, src, size);
    local_errno = result == NULL ? errno : 0;
}

void mm_mem_set(void *dst, const int value, const size_t size) {
    if (dst == NULL) { local_errno = EINVAL; return; }
    if (size == 0) { local_errno = 0; return; }
    memset(dst, value, size);
    local_errno = 0;
}

void mm_mem_zero(void *dst, const size_t size) {
    if (dst == NULL) { local_errno = EINVAL; return; }
    if (size == 0) { local_errno = 0; return; }
    memset(dst, 0, size);
    local_errno = 0;
}

int mm_mem_cmp(void const *ptr1, void const *ptr2, const size_t size) {
    if (ptr1 == NULL || ptr2 == NULL) { local_errno = EINVAL; return -2; }
    if (size == 0) { local_errno = 0; return 0; }
    local_errno = 0;
    return memcmp(ptr1, ptr2, size);
}

int mm_mem_cmp_const(void const *ptr1, void const *ptr2, const size_t size) {
    if (ptr1 == NULL || ptr2 == NULL) { local_errno = EINVAL; return -1; }
    if (size == 0) { local_errno = 0; return 1; }
    const auto pa = (const uint8_t*)ptr1;
    const auto pb = (const uint8_t*)ptr2;
    volatile uint8_t result = 0;
    for (size_t i = 0; i < size; i++) { result |= pa[i] ^ pb[i]; }
    return result;
}

int mm_mem_find(void const *haystack, const size_t haystack_size, void const *needle, const size_t needle_size) {
    if (haystack == NULL || needle == NULL) { local_errno = EINVAL; return -1; }
    if (needle_size == 0) { local_errno = 0; return 0; }
    if (haystack_size < needle_size) { local_errno = 0; return -1; }
    const auto result = memmem(haystack, haystack_size, needle, needle_size);
    local_errno = 0;
    return result == NULL ? -1 : result - haystack;
}

void* mm_mem_map(const size_t size) {
    if (size == 0) { local_errno = EINVAL; return NULL; }
    const auto ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (ptr == MAP_FAILED) { local_errno = errno; return NULL; }
    local_errno = 0;
    return ptr;
}

int mm_mem_unmap(void *addr, const size_t size) {
    if (addr == NULL || size == 0) { local_errno = EINVAL; return -1; }
    const auto result = munmap(addr, size);
    local_errno = result == -1 ? errno : 0;
    return result;
}

int mm_mem_lock(const void *addr, const size_t size) {
    if (addr == NULL || size == 0) { local_errno = EINVAL; return -1; }
    const auto result = mlock(addr, size);
    local_errno = result == -1 ? errno : 0;
    return result;
}

int mm_mem_unlock(const void *addr, const size_t size) {
    if (addr == NULL || size == 0) { local_errno = EINVAL; return -1; }
    const auto result = munlock(addr, size);
    local_errno = result == -1 ? errno : 0;
    return result;
}

int mm_mem_protect(void *addr, const size_t size, const int prot) {
    if (addr == NULL || size == 0) { local_errno = EINVAL; return -1; }
    if (prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC)) { local_errno = EINVAL; return -1; }
    const auto result = mprotect(addr, size, prot);
    local_errno = result == -1 ? errno : 0;
    return result;
}

size_t st_str_len(char const *restrict str, const size_t max_len) {
    if (str == NULL) { local_errno = EINVAL; return -1; }
    if (max_len == 0) { local_errno = 0; return 0; }
    local_errno = 0;
    return strnlen(str, max_len);
}

int st_str_cpy(char *restrict dst, const size_t dst_size, char const *restrict src) {
    if (dst == NULL || src == NULL || dst_size == 0) { local_errno = EINVAL; return -1; }
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
    local_errno = 0;
    return strlen(dst);
}

int st_str_cat(char *restrict dst, const size_t dst_size, char const *restrict src) {
    if (dst == NULL || src == NULL || dst_size == 0) { local_errno = EINVAL; return -1; }
    strncat(dst, src, dst_size - strlen(dst) - 1);
    dst[dst_size - 1] = '\0';
    local_errno = 0;
    return strlen(dst);
}

int st_str_cmp(char const *str1, char const *str2) {
    if (str1 == NULL || str2 == NULL) { local_errno = EINVAL; return -1; }
    local_errno = 0;
    return strcmp(str1, str2);
}

int st_str_case_cmp(char const *str1, char const *str2) {
    if (str1 == NULL || str2 == NULL) { local_errno = EINVAL; return -1; }
    local_errno = 0;
    return strcasecmp(str1, str2);
}

int st_str_find(char const *haystack, char const *needle) {
    if (haystack == NULL || needle == NULL) { local_errno = EINVAL; return -1; }
    local_errno = 0;
    const auto result = strstr(haystack, needle);
    return result == NULL ? -1 : result - haystack;
}

int st_str_case_find(char const *haystack, char const *needle) {
    if (haystack == NULL || needle == NULL) { local_errno = EINVAL; return -1; }
    local_errno = 0;
    const auto result = strcasestr(haystack, needle);
    return result == NULL ? -1 : result - haystack;
}

int so_socket(const int domain, const int type, const int protocol) {
    const auto fd = socket(domain, type, protocol);
    local_errno = fd == -1 ? errno : 0;
    return fd;
}

int so_close(const int socket_fd) {
    if (socket_fd < 0) { local_errno = EINVAL; return -1; }
    const auto result = close(socket_fd);
    local_errno = result == -1 ? errno : 0;
    return result;
}

int so_shutdown(const int socket_fd, const int how) {
    if (socket_fd < 0) { local_errno = EINVAL; return -1; }
    if (how != SHUT_RD && how != SHUT_WR && how != SHUT_RDWR) { local_errno = EINVAL; return -1; }
    const auto result = shutdown(socket_fd, how);
    local_errno = result == -1 ? errno : 0;
    return result;
}

int so_connect(const int socket_fd, char const *restrict host, const uint16_t port) {
    if (socket_fd < 0 || host == NULL) { local_errno = EINVAL; return -1; }
    _so_address_helper(SOCK_STREAM, IPPROTO_TCP, 0, connect(socket_fd, ai->ai_addr, ai->ai_addrlen) == 0)
}

int so_bind(const int socket_fd, char const *restrict host, const uint16_t port) {
    if (socket_fd < 0) { local_errno = EINVAL; return -1; }
    _so_address_helper(SOCK_STREAM, IPPROTO_TCP, 1, bind(socket_fd, ai->ai_addr, ai->ai_addrlen) == 0)
}

int so_listen(const int socket_fd, const int backlog) {
    if (socket_fd < 0 || backlog <= 0) { local_errno = EINVAL; return -1; }
    const auto result = listen(socket_fd, backlog);
    local_errno = result == -1 ? errno : 0;
    return result;
}

int so_accept(const int socket_fd, so_addr_t *restrict addr) {
    if (socket_fd < 0) { local_errno = EINVAL; return -1; }
    struct sockaddr_storage peer;
    socklen_t addr_len = sizeof(peer);
    int fd; do { fd = accept4(socket_fd, (struct sockaddr*)&peer, &addr_len, SOCK_CLOEXEC); } while (fd == -1 && errno == EINTR);
    if (fd == -1) { local_errno = errno; return -1; }
    _so_address_return_helper(peer, close(fd);)
    local_errno = 0;
    return fd;
}

ssize_t so_send(const int socket_fd, char const *data, const size_t size, const int flags) {
    if (socket_fd < 0 || data == NULL || size == 0) { local_errno = EINVAL; return -1; }
    ssize_t result; do { result = send(socket_fd, data, size, flags | MSG_NOSIGNAL); } while (result == -1 && errno == EINTR);
    local_errno = result == -1 ? errno : 0;
    return result;
}

ssize_t so_recv(const int socket_fd, char *buffer, const size_t size, const int flags) {
    if (socket_fd < 0 || buffer == NULL || size == 0) { local_errno = EINVAL; return -1; }
    ssize_t result; do { result = recv(socket_fd, buffer, size, flags); } while (result == -1 && errno == EINTR);
    local_errno = result == -1 ? errno : 0;
    return result;
}

ssize_t so_sendto(const int socket_fd, char const *data, const size_t size, char const *restrict host, const uint16_t port) {
    if (socket_fd < 0 || data == NULL || size == 0 || host == NULL) { local_errno = EINVAL; return -1; }
    _so_address_helper(SOCK_DGRAM, IPPROTO_UDP, 0, sendto(socket_fd, data, size, MSG_NOSIGNAL, ai->ai_addr, ai->ai_addrlen) >= 0)
}

ssize_t so_recvfrom(const int socket_fd, char *buffer, const size_t size, so_addr_t *restrict addr) {
    if (socket_fd < 0 || buffer == NULL || size == 0) { local_errno = EINVAL; return -1; }
    struct sockaddr_storage peer;
    socklen_t addr_len = sizeof(peer);
    ssize_t result; do { result = recvfrom(socket_fd, buffer, size, 0, (struct sockaddr*)&peer, &addr_len); } while (result == -1 && errno == EINTR);
    if (result == -1) { local_errno = errno; return -1; }
    _so_address_return_helper(peer,)
    return result;
}

int so_get_error(const int socket_fd) {
    if (socket_fd < 0) { local_errno = EINVAL; return -1; }
    auto error = 0;
    socklen_t len = sizeof(error);
    const auto result = getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &error, &len);
    if (result == -1) { local_errno = errno; return -1; }
    local_errno = 0;
    return error;
}

int so_set_nonblocking(const int socket_fd, const bool non_blocking) {
    if (socket_fd < 0) { local_errno = EINVAL; return -1; }
    auto flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags == -1) { local_errno = errno; return -1; }
    flags = non_blocking ? flags | O_NONBLOCK : flags & ~O_NONBLOCK;
    const auto result = fcntl(socket_fd, F_SETFL, flags);
    local_errno = result == -1 ? errno : 0;
    return result;
}

int so_set_recv_timeout(const int socket_fd, const int timeout_ms) {
    if (socket_fd < 0) { local_errno = EINVAL; return -1; }
    const struct timeval tv = { .tv_sec = timeout_ms / 1000, .tv_usec = timeout_ms % 1000 * 1000 };
    const auto result = setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    local_errno = result == -1 ? errno : 0;
    return result;
}

int so_set_send_timeout(const int socket_fd, const int timeout_ms) {
    if (socket_fd < 0) { local_errno = EINVAL; return -1; }
    const struct timeval tv = { .tv_sec = timeout_ms / 1000, .tv_usec = timeout_ms % 1000 * 1000 };
    const auto result = setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    local_errno = result == -1 ? errno : 0;
    return result;
}

int so_set_reuseaddr(const int socket_fd, const bool reuse) {
    if (socket_fd < 0) { local_errno = EINVAL; return -1; }
    const auto v = reuse ? 1 : 0;
    const auto result = setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &v, sizeof(v));
    local_errno = result == -1 ? errno : 0;
    return result;
}

int so_set_keepalive(const int socket_fd, const bool keepalive) {
    if (socket_fd < 0) { local_errno = EINVAL; return -1; }
    const auto v = keepalive ? 1 : 0;
    const auto result = setsockopt(socket_fd, SOL_SOCKET, SO_KEEPALIVE, &v, sizeof(v));
    local_errno = result == -1 ? errno : 0;
    return result;
}

int so_set_nodelay(const int socket_fd, const bool nodelay) {
    if (socket_fd < 0) { local_errno = EINVAL; return -1; }
    const auto v = nodelay ? 1 : 0;
    const auto result = setsockopt(socket_fd, IPPROTO_TCP, TCP_NODELAY, &v, sizeof(v));
    local_errno = result == -1 ? errno : 0;
    return result;
}

int so_getsockname(const int socket_fd, so_addr_t *restrict addr) {
    if (socket_fd < 0 || addr == NULL) { local_errno = EINVAL; return -1; }
    struct sockaddr_storage local;
    socklen_t addr_len = sizeof(local);
    const auto result = getsockname(socket_fd, (struct sockaddr*)&local, &addr_len);
    if (result == -1) { local_errno = errno; return -1; }
    _so_address_return_helper(local,)
    local_errno = 0;
    return 0;
}

int so_getpeername(const int socket_fd, so_addr_t *restrict addr) {
    if (socket_fd < 0 || addr == NULL) { local_errno = EINVAL; return -1; }
    struct sockaddr_storage peer;
    socklen_t addr_len = sizeof(peer);
    const auto result = getpeername(socket_fd, (struct sockaddr*)&peer, &addr_len);
    if (result == -1) { local_errno = errno; return -1; }
    _so_address_return_helper(peer,)
    local_errno = 0;
    return 0;
}

int ti_gettime(const clockid_t clock, ti_duration_t *restrict out) {
    struct timespec ts;
    const auto result = clock_gettime(clock, &ts);
    if (result == -1) { local_errno = errno; return -1; }
    if (ts.tv_sec < 0 || ts.tv_nsec < 0) { local_errno = EINVAL; return -1; }
    out->seconds = ts.tv_sec;
    out->nanoseconds = ts.tv_nsec;
    local_errno = 0;
    return 0;
}

int ti_getres(const clockid_t clock, ti_duration_t *restrict out) {
    struct timespec ts;
    const auto result = clock_getres(clock, &ts);
    if (result == -1) { local_errno = errno; return -1; }
    if (ts.tv_sec < 0 || ts.tv_nsec < 0) { local_errno = EINVAL; return -1; }
    out->seconds = ts.tv_sec;
    out->nanoseconds = ts.tv_nsec;
    local_errno = 0;
    return 0;
}

int ti_nanosleep(const clockid_t clock, ti_duration_t const *duration) {
    if (duration == NULL) { local_errno = EINVAL; return -1; }
    if (duration->seconds < 0 || duration->nanoseconds < 0) { local_errno = EINVAL; return -1; }
    if (duration->nanoseconds >= 1000000000) { local_errno = EINVAL; return -1; }

    struct timespec req = { .tv_sec = duration->seconds, .tv_nsec = duration->nanoseconds };
    struct timespec rem = { .tv_sec = 0, .tv_nsec = 0 };
    while (clock_nanosleep(clock, 0, &req, &rem) == -1) {
        if (errno != EINTR) { local_errno = errno; return -1; }
        req = rem;
    }

    local_errno = 0;
    return 0;
}

int ti_localtime(const ti_duration_t *restrict ts, ti_breakdown_t *restrict breakdown) {
    if (ts == NULL || breakdown == NULL) { local_errno = EINVAL; return -1; }
    if (ts->seconds < 0 || ts->nanoseconds < 0) { local_errno = EINVAL; return -1; }
    if (ts->nanoseconds >= 1000000000) { local_errno = EINVAL; return -1; }

    const auto t = ts->seconds;
    struct tm local;
    const auto result = localtime_r(&t, &local);
    if (result == NULL) { local_errno = errno; return -1; }

    *breakdown = (ti_breakdown_t){
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
    };

    local_errno = 0;
    return 0;
}

int ti_mktime(const ti_breakdown_t *restrict bd, ti_duration_t *restrict out) {
    if (bd == NULL || out == NULL) { local_errno = EINVAL; return -1; }
    if (bd->month < 1 || bd->month > 12 || bd->day < 1 || bd->day > 31 || bd->hour > 23 || bd->minute > 59 || bd->second > 60) {
        local_errno = EINVAL;
        return -1;
    }

    struct tm local = {
        .tm_year = bd->year - 1900,
        .tm_mon = bd->month - 1,
        .tm_mday = bd->day,
        .tm_hour = bd->hour,
        .tm_min = bd->minute,
        .tm_sec = bd->second,
    };

    const auto t = mktime(&local);
    if (t == -1) { local_errno = errno; return -1; }

    out->seconds = t;
    out->nanoseconds = 0;
    local_errno = 0;
    return 0;
}

int ti_local_tz_name(char *restrict buffer) {
    if (buffer == NULL) { local_errno = EINVAL; return -1; }
    const auto t = time(NULL);

    if (t == -1) { local_errno = errno; return -1; }
    struct tm local_tm;

    const auto result = localtime_r(&t, &local_tm) == NULL;
    if (result == -1) { local_errno = errno; return -1; }

    const auto tz = local_tm.tm_zone;
    if (tz == NULL) { local_errno = EINVAL; return -1; }
    strncpy(buffer, tz, 255);
    buffer[255] = '\0';
    local_errno = 0;
    return 0;
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
    const auto result = setuid(uid);
    local_errno = result == -1 ? errno : 0;
    return local_errno;
}

gid_t pr_get_gid() {
    return getgid();
}

int pr_set_gid(const gid_t gid) {
    const auto result = setgid(gid);
    local_errno = result == -1 ? errno : 0;
    return local_errno;
}

uid_t pr_get_euid() {
    return geteuid();
}

gid_t pr_get_egid() {
    return getegid();
}

int pr_getenv(char const *restrict key, char *restrict val) {
    if (key == NULL) { local_errno = EINVAL; return -1; }
    const auto result = getenv(key);
    if (result == NULL) { local_errno = ENOENT; return -1; }
    strncpy(val, result, 255);
    val[255] = '\0';
    local_errno = 0;
    return 0;
}

int pr_setenv(char const *restrict key, char const *restrict val, const bool overwrite) {
    if (key == NULL || val == NULL) { local_errno = EINVAL; return -1; }
    if (key[0] == '\0' || strchr(key, '=') != NULL) { local_errno = EINVAL; return -1; }
    const auto result = setenv(key, val, overwrite);
    local_errno = result == -1 ? errno : 0;
    return result;
}

int pr_unsetenv(char const *restrict key) {
    if (key == NULL) { local_errno = EINVAL; return -1; }
    const auto result = unsetenv(key);
    local_errno = result == -1 ? errno : 0;
    return result;
}

int pr_envvars(pr_env_t *restrict env) {
    if (env == NULL) { local_errno = EINVAL; return -1; }
    size_t count = 0;
    while (environ[count] != NULL) { count++; }
    env->count = count;
    env->vars = malloc(count * sizeof(pr_env_var_t));
    if (env->vars == NULL) { local_errno = ENOMEM; return -1; }
    for (size_t i = 0; i < count; i++) {
        const auto eq_pos = strchr(environ[i], '=');
        if (eq_pos == NULL) {
            env->vars[i].key[0] = '\0';
            env->vars[i].val[0] = '\0';
            continue;
        }
        const auto key_len = eq_pos - environ[i];
        const auto val_len = strlen(eq_pos + 1);
        strncpy(env->vars[i].key, environ[i], key_len);
        strncpy(env->vars[i].val, eq_pos + 1, val_len);
        env->vars[i].key[key_len] = '\0';
        env->vars[i].val[val_len] = '\0';
    }
    local_errno = 0;
    return 0;
}

int pr_free_envvars(pr_env_t *env) {
    if (env == NULL || env->vars == NULL) { local_errno = EINVAL; return -1; }
    free(env->vars);
    env->vars = NULL;
    env->count = 0;
    local_errno = 0;
    return 0;
}

int pr_exec(char const *restrict path, char const *const *restrict argv, char const *const *restrict envp, const int stdin_fd, const int stdout_fd, const int stderr_fd) {
    if (path == NULL || argv == NULL) { local_errno = EINVAL; return -1; }
    if (access(path, X_OK) == -1) { local_errno = errno; return -1; }

    const auto pid = fork();
    if (pid == -1) { local_errno = errno; return -1; }
    if (pid == 0) {
        if (stdin_fd != -1 && dup2(stdin_fd, STDIN_FILENO) == -1) { _exit(127); }
        if (stdout_fd != -1 && dup2(stdout_fd, STDOUT_FILENO) == -1) { _exit(127); }
        if (stderr_fd != -1 && dup2(stderr_fd, STDERR_FILENO) == -1) { _exit(127); }
        execve(path, (char *const *)argv, envp ? (char**)envp : environ);
        _exit(127);
    }

    local_errno = 0;
    return 0;
}

int pr_waitpid(const int pid, pr_wait_result_t *restrict result) {
    if (pid < 0 || result == NULL) { local_errno = EINVAL; return -1; }
    int status; pid_t res; do { res = waitpid(pid, &status, 0); } while (res == -1 && errno == EINTR);
    if (res == -1) { local_errno = errno; return -1; }
    *result = (pr_wait_result_t){
        .exited = WIFEXITED(status) ? 1 : 0,
        .signaled = WIFSIGNALED(status) ? 1 : 0,
        .stopped = WIFSTOPPED(status) ? 1 : 0,
        .exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1,
        .signal = WIFSIGNALED(status) ? WTERMSIG(status) : -1,
    };
    local_errno = 0;
    return 0;
}

int pr_waitpid_nowait(const int pid, pr_wait_result_t *restrict result) {
    if (pid < 0 || result == NULL) { local_errno = EINVAL; return -1; }
    int status; const auto res = waitpid(pid, &status, WNOHANG);
    if (res == -1) { local_errno = errno; return -1; }
    *result = (pr_wait_result_t){
        .exited = WIFEXITED(status) ? 1 : 0,
        .signaled = WIFSIGNALED(status) ? 1 : 0,
        .stopped = WIFSTOPPED(status) ? 1 : 0,
        .exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1,
        .signal = WIFSIGNALED(status) ? WTERMSIG(status) : -1,
    };
    local_errno = 0;
    return 0;
}

int pr_signal(const pid_t pid, const int signal) {
    if (pid < 0 || signal < 0) { local_errno = EINVAL; return -1; }
    const auto result = kill(pid, signal);
    local_errno = result == -1 ? errno : 0;
    return result;
}

int pr_is_running(const int pid) {
    if (pid < 0) { local_errno = EINVAL; return -1; }
    const auto ret = kill(pid, 0);
    if (ret == 0) { local_errno = 0; return 1; }
    if (errno == ESRCH) { local_errno = 0; return 0; }
    if (errno == EPERM) { local_errno = 0; return 1; }
    local_errno = errno;
    return -1;
}

int pr_get_cwd(char *restrict buffer) {
    if (buffer == NULL) { local_errno = EINVAL; return -1; }
    const auto cwd = getcwd(buffer, 256);
    if (cwd == NULL) { local_errno = errno; return -1; }
    local_errno = 0;
    return 0;
}

int pr_set_cwd(char const *restrict path) {
    if (path == NULL) { local_errno = EINVAL; return -1; }
    const auto result = chdir(path);
    local_errno = result == -1 ? errno : 0;
    return result;
}

int pr_exit(const int status) {
    _exit(status);
}

int pr_exit_clean(const int status) {
    exit(status);
}

int pr_abort() {
    abort();
}

int rn_csprng_bytes(void *restrict buffer, const size_t size) {
    if (buffer == NULL || size == 0) { local_errno = EINVAL; return -1; }
    const auto ptr = (uint8_t *) buffer;
    size_t offset = 0;

    while (offset < size) {
        const auto result = getrandom(ptr + offset, size - offset, 0);
        if (result == -1) {
            if (errno == EINTR) { continue; }
            local_errno = errno; return -1;
        }
        offset += result;
    }

    local_errno = 0;
    return 0;
}

uint32_t rn_csprng_u32(void) {
    uint32_t result;
    if (rn_csprng_bytes(&result, sizeof(result)) == -1) { return 0; }
    return result;
}

uint64_t rn_csprng_u64(void) {
    uint64_t result;
    if (rn_csprng_bytes(&result, sizeof(result)) == -1) { return 0; }
    return result;
}

uint64_t rn_csprng_range(const uint64_t min, const uint64_t max) {
    if (min > max) { local_errno = EINVAL; return 0; }
    const auto range = max - min + 1;
    const auto threshold = (UINT64_MAX - range + 1) % range;
    while (1) {
        const auto val = rn_csprng_u64();
        if (val >= threshold) { return min + val % range; }
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
    local_errno = 0;
    return 0;
}

int rn_prng_reset(void) {
    SPPC_PRNG_SEEDED = false;
    return _prng_ensure_seeded();
}

uint64_t rn_prng_next_u64(void) {
    if (_prng_ensure_seeded() == -1) { return 0; }
    local_errno = 0;
    return _xoshiro256ss_next(SPPC_PRNG_STATE.s);
}

double rn_prng_next_double(void) {
    if (_prng_ensure_seeded() == -1) { return 0.0; }
    local_errno = 0;
    const auto random_u64 = _xoshiro256ss_next(SPPC_PRNG_STATE.s);
    return (random_u64 >> 11) * (1.0 / (UINT64_C(1) << 53));
}

uint64_t rn_prng_next_range(const uint64_t min, const uint64_t max) {
    if (min > max) { local_errno = EINVAL; return 0; }
    if (_prng_ensure_seeded() == -1) { return 0; }
    const auto range = max - min + 1;
    const auto threshold = (UINT64_MAX - range + 1) % range;
    while (1) {
        const auto val = _xoshiro256ss_next(SPPC_PRNG_STATE.s);
        if (val >= threshold) { return min + val % range; }
    }
}

int rn_prng_fill_bytes(void *restrict buffer, const size_t size) {
    if (buffer == NULL) { local_errno = EINVAL; return -1; }
    if (_prng_ensure_seeded() == -1) { return -1; }

    auto ptr = (uint8_t*)buffer;
    auto remaining = size;
    while (remaining >= 8) {
        const auto random_u64 = _xoshiro256ss_next(SPPC_PRNG_STATE.s);
        memcpy(ptr, &random_u64, 8);
        ptr += 8;
        remaining -= 8;
    }
    if (remaining > 0) {
        const auto random_u64 = _xoshiro256ss_next(SPPC_PRNG_STATE.s);
        memcpy(ptr, &random_u64, remaining);
    }
    local_errno = 0;
    return 0;
}

int rn_prng_jump(void) {
    if (_prng_ensure_seeded() == -1) { return -1; }

    static constexpr uint64_t JUMP[] = {
        0x180ec6d33cfd0abaULL, 0xd5a61266f0c9392cULL,
        0xa9582618e03fc9aaULL, 0x39abdc4529b1661cULL
    };

    int64_t s0 = 0, s1 = 0, s2 = 0, s3 = 0;
    for (auto i = 0; i < 4; i++) {
        for (auto b = 0; b < 64; b++) {
            if (JUMP[i] & UINT64_C(1) << b) {
                s0 ^= SPPC_PRNG_STATE.s[0];
                s1 ^= SPPC_PRNG_STATE.s[1];
                s2 ^= SPPC_PRNG_STATE.s[2];
                s3 ^= SPPC_PRNG_STATE.s[3];
            }
            _xoshiro256ss_next(SPPC_PRNG_STATE.s);
        }
    }

    SPPC_PRNG_STATE.s[0] = s0; SPPC_PRNG_STATE.s[1] = s1;
    SPPC_PRNG_STATE.s[2] = s2; SPPC_PRNG_STATE.s[3] = s3;
    local_errno = 0;
    return 0;
}

int64_t sys_conf(const int what) {
    const auto result = sysconf(what);
    if (result == -1) { local_errno = errno; return -1; }
    local_errno = 0;
    return result;
}

uint64_t sys_getid() {
    local_errno = 0;
    return gettid();
}

int sys_info(sys_info_t *restrict out) {
    if (out == NULL) { local_errno = EINVAL; return -1; }
    struct sysinfo raw;
    const auto result = sysinfo(&raw);
    if (result == -1) { local_errno = errno; return -1; }

    *out = (sys_info_t){
        .total = raw.totalram,
        .free = raw.freeram,
        .shared = raw.sharedram,
        .buffered = raw.bufferram,
        .swap_total = raw.totalswap,
        .swap_free = raw.freeswap,
        .uptime_seconds = raw.uptime,
        .procs = raw.procs,
    };

    local_errno = 0;
    return 0;
}

int sys_loadavg(sys_loadavg_t *restrict out) {
    if (out == NULL) { local_errno = EINVAL; return -1; }

    struct sysinfo raw;
    const auto result = sysinfo(&raw);
    if (result == -1) { local_errno = errno; return -1; }

    constexpr auto scale = (double)(1 << SI_LOAD_SHIFT);
    *out = (sys_loadavg_t){
        .one = (double)raw.loads[0] / scale,
        .five = (double)raw.loads[1] / scale,
        .ten = (double)raw.loads[2] / scale,
    };

    local_errno = 0;
    return 0;
}

int sys_uname(sys_uname_t *restrict out) {
    if (out == NULL) { local_errno = EINVAL; return -1; }
    struct utsname raw;
    const auto result = uname(&raw);
    if (result == -1) { local_errno = errno; return -1; }

    *out = (sys_uname_t){
        .sysname = raw.sysname,
        .nodename = raw.nodename,
        .domainname = raw.domainname,
        .release = raw.release,
        .version = raw.version,
        .machine = raw.machine,
    };

    local_errno = 0;
    return 0;
}

int sys_gethostname(char *restrict buffer) {
    if (buffer == NULL) { local_errno = EINVAL; return -1; }
    const auto result = gethostname(buffer, 256);
    if (result == -1) { local_errno = errno; return -1; }
    local_errno = 0;
    return 0;
}

int sys_sethostname(char const *restrict name) {
    if (name == NULL) { local_errno = EINVAL; return -1; }
    const auto result = sethostname(name, strlen(name));
    local_errno = result == -1 ? errno : 0;
    return result;
}

int sys_getrlimit(const int resource, sys_rlimit_t *restrict out) {
    if (out == NULL) { local_errno = EINVAL; return -1; }
    struct rlimit raw;
    const auto result = getrlimit(resource, &raw);
    if (result == -1) { local_errno = errno; return -1; }
    *out = (sys_rlimit_t){
        .soft = raw.rlim_cur == RLIM_INFINITY ? UINT64_MAX : raw.rlim_cur,
        .hard = raw.rlim_max == RLIM_INFINITY ? UINT64_MAX : raw.rlim_max,
    };

    local_errno = 0;
    return 0;
}

int sys_setrlimit(const int resource, sys_rlimit_t const *limit) {
    if (limit == NULL) { local_errno = EINVAL; return -1; }
    const struct rlimit raw = {
        .rlim_cur = limit->soft == UINT64_MAX ? RLIM_INFINITY : limit->soft,
        .rlim_max = limit->hard == UINT64_MAX ? RLIM_INFINITY : limit->hard,
    };
    const auto result = setrlimit(resource, &raw);
    local_errno = result == -1 ? errno : 0;
    return result;
}

int64_t sys_cache_line_size(void) {
    const auto result = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
    if (result == -1 || result == 0) { local_errno = errno; return 64; }
    local_errno = 0;
    return result;
}

uint64_t pt_thread_spawn(void*(*start_routine)(void *)) {
    if (start_routine == NULL) { local_errno = EINVAL; return 0; }
    uint64_t thread;
    const auto err = pthread_create(&thread, NULL, start_routine, NULL);
    if (err != 0) { local_errno = errno; return 0; }
    local_errno = 0;
    return thread;
}

int pt_thread_join(const uint64_t handle) {
    if (handle <= 0) { local_errno = EINVAL; return -1; }
    const auto err = pthread_join(handle, NULL);
    local_errno = err == -1 ? err : 0;
    return err;
}

int pt_thread_detach(const uint64_t handle) {
    if (handle <= 0) { local_errno = EINVAL; return -1; }
    const auto err = pthread_detach(handle);
    local_errno = err == -1 ? err : 0;
    return err;
}

int pt_thread_signal(const uint64_t handle, const int sig) {
    if (handle <= 0) { local_errno = EINVAL; return -1; }
    const auto err = pthread_kill(handle, sig);
    local_errno = err == -1 ? err : 0;
    return err;
}

int pt_thread_cancel(const uint64_t handle) {
    if (handle <= 0) { local_errno = EINVAL; return -1; }
    const auto err = pthread_cancel(handle);
    local_errno = err == -1 ? err : 0;
    return err;
}

uint64_t pt_thread_self() {
    local_errno = 0;
    return pthread_self();
}

int pt_thread_equal(const uint64_t handle1, const uint64_t handle2) {
    if (handle1 <= 0 || handle2 <= 0) { local_errno = EINVAL; return -1; }
    const auto result = pthread_equal(handle1, handle2);
    local_errno = 0;
    return result;
}

int pt_thread_yield() {
    const auto err = sched_yield();
    local_errno = err == -1 ? err : 0;
    return err;
}

uint64_t pt_mutex_create(const bool recursive) {
    const auto m = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
    if (m == NULL) { local_errno = ENOMEM; return 0; }

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, recursive ? PTHREAD_MUTEX_RECURSIVE : PTHREAD_MUTEX_ERRORCHECK);

    const auto err = pthread_mutex_init(m, &attr);
    pthread_mutexattr_destroy(&attr);
    if (err != 0) { free(m); local_errno = err; return 0; }

    local_errno = 0;
    return (uintptr_t)m;
}

int pt_mutex_lock(const uint64_t handle) {
    if (handle == 0) { local_errno = EINVAL; return -1; }
    const auto err = pthread_mutex_lock((pthread_mutex_t*)(uintptr_t)handle);
    local_errno = err == -1 ? err : 0;
    return err;
}

int pt_mutex_try_lock(const uint64_t handle) {
    if (handle == 0) { local_errno = EINVAL; return -1; }
    const auto err = pthread_mutex_trylock((pthread_mutex_t*)(uintptr_t)handle);
    if (err == 0) { local_errno = 0; return 1; }
    if (err == EBUSY) { local_errno = 0; return 0; }
    local_errno = err == -1 ? err : 0;
    return -1;
}

int pt_mutex_timeout_lock(const uint64_t handle, ti_duration_t const *timeout) {
    if (handle == 0 || timeout == NULL) { local_errno = EINVAL; return -1; }
    if (timeout->seconds < 0 || timeout->nanoseconds < 0) { local_errno = EINVAL; return -1; }
    if (timeout->nanoseconds >= 1000000000) { local_errno = EINVAL; return -1; }
    _pt_timeout_helper(mutex, lock);
    return -1;
}

int pt_mutex_unlock(const uint64_t handle) {
    if (handle == 0) { local_errno = EINVAL; return -1; }
    const auto err = pthread_mutex_unlock((pthread_mutex_t*)(uintptr_t)handle);
    local_errno = err == -1 ? err : 0;
    return err != 0 ? -1 : 0;
}

int pt_mutex_destroy(const uint64_t handle) {
    if (handle == 0) { local_errno = EINVAL; return -1; }
    const auto m = (pthread_mutex_t*)(uintptr_t)handle;
    const auto err = pthread_mutex_destroy(m);
    if (err != 0) { local_errno = err; return -1; }
    free(m);
    local_errno = 0;
    return 0;
}

uint64_t pt_condvar_create() {
    const auto c = (pthread_cond_t*)malloc(sizeof(pthread_cond_t));
    if (c == NULL) { local_errno = ENOMEM; return 0; }

    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setpshared(&attr, CLOCK_MONOTONIC);

    const auto err = pthread_cond_init(c, &attr);
    pthread_condattr_destroy(&attr);

    if (err != 0) { free(c); local_errno = err; return 0; }
    local_errno = 0;
    return (uintptr_t)c;
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

uint64_t pt_rwlock_create() {
    const auto r = (pthread_rwlock_t*)malloc(sizeof(pthread_rwlock_t));
    if (r == NULL) { local_errno = ENOMEM; return 0; }
    const auto err = pthread_rwlock_init(r, NULL);
    if (err != 0) { free(r); local_errno = err; return 0; }
    local_errno = 0;
    return (uintptr_t)r;
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

uint64_t pt_barrier_create(const uint32_t count) {
    if (count == 0) { local_errno = EINVAL; return 0; }
    const auto b = (pthread_barrier_t*)malloc(sizeof(pthread_barrier_t));
    if (b == NULL) { local_errno = ENOMEM; return 0; }
    const auto err = pthread_barrier_init(b, NULL, count);
    if (err != 0) { free(b); local_errno = err; return 0; }
    local_errno = 0;
    return (uintptr_t)b;
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

uint64_t pt_spinlock_create() {
    const auto s = (pthread_spinlock_t*)malloc(sizeof(pthread_spinlock_t));
    if (s == NULL) { local_errno = ENOMEM; return 0; }
    const auto err = pthread_spin_init(s, PTHREAD_PROCESS_PRIVATE);
    if (err != 0) { free(s); local_errno = err; return 0; }
    local_errno = 0;
    return (uintptr_t)s;
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
