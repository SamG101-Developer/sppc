#include <sppc/sppc.h>
#include <errno.h>       // error number definitions
#include <unistd.h>      // stream operations and macros
#include <string.h>      // string operations and macros
#include <fcntl.h>       // file operations and macros
#include <netdb.h>       // network database operations and macros
#include <stdlib.h>      // memory allocation functions
#include <stdio.h>       // standard I/O operations and macros
#include <sys/file.h>    // file locking operations and macros
#include <sys/socket.h>  // socket operations and macros
#include <sys/wait.h>    // process control operations and macros
#include <sys/stat.h>    // file status operations and macros
#include <netinet/tcp.h> // TCP protocol definitions
#include <time.h>        // time functions and macros
#include <pthread.h>     // POSIX threads operations and macros


typedef struct {
    pthread_mutex_t primitive;
    bool in_use;
} mutex_table_t;

typedef struct {
    pthread_cond_t primitive;
    bool in_use;
} cond_table_t;

typedef struct {
    pthread_rwlock_t primitive;
    bool in_use;
} rwlock_table_t;

typedef struct {
    pthread_barrier_t primitive;
    bool in_use;
} barrier_table_t;

typedef struct {
    pthread_spinlock_t primitive;
    bool in_use;
} spinlock_table_t;


static unsigned long long RNG_STATE = 0;

static mutex_table_t *MUTEX_TABLE = nullptr;
static size_t MUTEX_CAP = 0;
static pthread_mutex_t MUTEX_TABLE_MTX = PTHREAD_MUTEX_INITIALIZER;

static cond_table_t *COND_TABLE = nullptr;
static size_t COND_CAP = 0;
static pthread_mutex_t COND_TABLE_MTX = PTHREAD_MUTEX_INITIALIZER;

static rwlock_table_t *RWLOCK_TABLE = nullptr;
static size_t RWLOCK_CAP = 0;
static pthread_mutex_t RWLOCK_TABLE_MTX = PTHREAD_MUTEX_INITIALIZER;

static barrier_table_t *BARRIER_TABLE = nullptr;
static size_t BARRIER_CAP = 0;
static pthread_mutex_t BARRIER_TABLE_MTX = PTHREAD_MUTEX_INITIALIZER;

static spinlock_table_t *SPINLOCK_TABLE = nullptr;
static size_t SPINLOCK_CAP = 0;
static pthread_mutex_t SPINLOCK_TABLE_MTX = PTHREAD_MUTEX_INITIALIZER;


#define _net_address_helper(socktype, proto, bind, cond)               \
    char port_str[6];                                                  \
    snprintf(port_str, sizeof(port_str), "%u", port);                  \
                                                                       \
    struct addrinfo hints = {};                                        \
    memset(&hints, 0, sizeof(hints));                                  \
    hints.ai_family = AF_UNSPEC;                                       \
    hints.ai_socktype = socktype;                                      \
    hints.ai_protocol = proto;                                         \
    if(bind) {                                                         \
        hints.ai_flags = AI_PASSIVE;                                   \
    }                                                                  \
                                                                       \
    struct addrinfo *res = nullptr;                                    \
    if (getaddrinfo((const char*)host, port_str, &hints, &res) != 0) { \
        return -1;                                                     \
    }                                                                  \
    auto ret = -1;                                                     \
    for (auto ai = res; ai != nullptr; ai = ai->ai_next) {             \
        if (cond) {                                                    \
            ret = 0;                                                   \
            break;                                                     \
        }                                                              \
    }                                                                  \
                                                                       \
    freeaddrinfo(res);                                                 \
    return ret;


#define _thread_create_helper(MASTER, master_t, master_f, ...)                                                               \
    pthread_mutex_lock(&MASTER ## _TABLE_MTX);                                                                   \
                                                                                                                 \
    if (MASTER ## _TABLE == nullptr) {                                                                           \
        MASTER ## _CAP = 16;                                                                                     \
        MASTER ## _TABLE = (master_t ## _table_t*)calloc(MASTER ## _CAP, sizeof(master_t ## _table_t));              \
    }                                                                                                            \
                                                                                                                 \
                                                                                                                 \
    for (size_t i = 0; i < MASTER ## _CAP; ++i) {                                                                \
        if (!MASTER ## _TABLE[i].in_use) {                                                                       \
            MASTER ## _TABLE[i].in_use = true;                                                                   \
            pthread_ ## master_f ## _init(&MASTER ## _TABLE[i].primitive, 0 __VA_OPT__(, __VA_ARGS__));                   \
            pthread_mutex_unlock(&MASTER ## _TABLE_MTX);                                                         \
            return (int)i;                                                                                       \
        }                                                                                                        \
    }                                                                                                            \
                                                                                                                 \
                                                                                                                 \
    const auto new_cap = MASTER ## _CAP * 2;                                                                     \
    const auto new_table = (master_t ## _table_t*)realloc(MASTER ## _TABLE, new_cap * sizeof(master_t ## _table_t)); \
    if (new_table == nullptr) {                                                                                  \
        pthread_mutex_unlock(&MASTER ## _TABLE_MTX);                                                             \
        return -1;                                                                                               \
    }                                                                                                            \
                                                                                                                 \
    MASTER ## _TABLE = new_table;                                                                                \
                                                                                                                 \
    for (auto i = MASTER ## _CAP; i < new_cap; ++i) {                                                            \
        MASTER ## _TABLE[i].in_use = false;                                                                      \
    }                                                                                                            \
                                                                                                                 \
    MASTER ## _CAP = new_cap;                                                                                    \
    const auto id = (int)MASTER ## _CAP;                                                              \
    MASTER ## _TABLE[MASTER ## _CAP].in_use = true;                                                              \
    pthread_ ## master_f ##_init(&MASTER ## _TABLE[MASTER ## _CAP].primitive, 0 __VA_OPT__(, __VA_ARGS__));               \
    pthread_mutex_unlock(&MASTER ## _TABLE_MTX);                                                                 \
    return id


#define _thread_destroy_helper(MASTER, master_t, master_f)                                  \
    pthread_mutex_lock(&MASTER ## _TABLE_MTX);                                  \
    pthread_ ## master_f ## _destroy(&MASTER ## _TABLE[master_t ## _id].primitive); \
    MASTER ## _TABLE[master_t ## _id].in_use = false;                             \
    pthread_mutex_unlock(&MASTER ## _TABLE_MTX);                                \
    return 0


size_t fd_read(void *restrict buffer, const size_t size, const size_t count, const int fd) {
    if (buffer == nullptr || size == 0 || count == 0 || fd < 0) { return 0; }
    const auto bytes_read = read(fd, buffer, size * count);
    return bytes_read < 0 ? bytes_read : (size_t)bytes_read / size;
}


size_t fd_write(void const *restrict buffer, const size_t size, const size_t count, const int fd) {
    if (buffer == nullptr || size == 0 || count == 0 || fd < 0) { return 0; }
    const auto bytes_written = write(fd, buffer, size * count);
    return bytes_written < 0 ? bytes_written : (size_t)bytes_written / size;
}


int fd_open(unsigned char const *restrict filename, unsigned char const *restrict mode) {
    if (filename == nullptr || mode == nullptr) { return -1; }

    auto flags = 0;
    if (strcmp((const char*)mode, "r") == 0) {
        flags = O_RDONLY;
    }
    else if (strcmp((const char*)mode, "r+") == 0) {
        flags = O_RDWR;
    }
    else if (strcmp((const char*)mode, "w") == 0) {
        flags = O_WRONLY | O_CREAT | O_TRUNC;
    }
    else if (strcmp((const char*)mode, "w+") == 0) {
        flags = O_RDWR | O_CREAT | O_TRUNC;
    }
    else if (strcmp((const char*)mode, "a") == 0) {
        flags = O_WRONLY | O_CREAT | O_APPEND;
    }
    else if (strcmp((const char*)mode, "a+") == 0) {
        flags = O_RDWR | O_CREAT | O_APPEND;
    }
    else {
        return -1; // Invalid mode
    }

    const auto fd = open((const char*)filename, flags, 0644);
    return fd < 0 ? -1 : fd;
}


int fd_close(const int fd) {
    if (fd < 0) { return -1; }
    return close(fd);
}


int fd_flush(const int fd) {
    if (fd < 0) { return -1; }
    return fsync(fd);
}


int fd_seek(const int fd, const long long offset, const int whence) {
    if (fd < 0) { return -1; }
    return lseek(fd, offset, whence);
}


long long fd_tell(const int fd) {
    if (fd < 0) { return -1; }
    return lseek(fd, 0, SEEK_CUR);
}


int fd_truncate(const int fd, const long long length) {
    if (fd < 0) { return -1; }
    return ftruncate(fd, length);
}


int fd_lock_ex(const int fd, const bool non_blocking) {
    if (fd < 0) { return -1; }
    return flock(fd, LOCK_EX | (non_blocking ? LOCK_NB : 0));
}


int fd_lock_sh(const int fd, const bool non_blocking) {
    if (fd < 0) { return -1; }
    return flock(fd, LOCK_SH | (non_blocking ? LOCK_NB : 0));
}


int fd_unlock(const int fd) {
    if (fd < 0) { return -1; }
    return flock(fd, LOCK_UN);
}


void* mem_alloc(const size_t size) {
    if (size == 0) { return nullptr; }
    return malloc(size);
}


void* mem_calloc(const size_t num, const size_t size) {
    if (num == 0 || size == 0) { return nullptr; }
    return calloc(num, size);
}


void mem_dealloc(void *ptr) {
    if (ptr == nullptr) { return; }
    free(ptr);
}


void* mem_realloc(void *ptr, const size_t new_size) {
    return realloc(ptr, new_size);
}


int net_sock_init(const int domain, const int type, const int protocol) {
    return socket(domain, type, protocol);
}


long long net_sock_send(const int socket_fd, unsigned char const *data, const size_t size) {
    if (socket_fd < 0 || data == nullptr || size == 0) { return -1; }
    return send(socket_fd, data, size, 0);
}


long long net_sock_recv(const int socket_fd, unsigned char *buffer, const size_t size) {
    if (socket_fd < 0 || buffer == nullptr || size == 0) { return -1; }
    return recv(socket_fd, buffer, size, 0);
}


int net_sock_conn(const int socket_fd, unsigned char const *restrict host, const unsigned short port) {
    if (host == nullptr) { return -1; }
    _net_address_helper(SOCK_STREAM, IPPROTO_TCP, false, connect(socket_fd, ai->ai_addr, ai->ai_addrlen) == 0)
}


long long net_sock_bind(const int socket_fd, unsigned char const *restrict host, const unsigned short port) {
    if (host == nullptr) { return -1; }
    _net_address_helper(SOCK_STREAM, IPPROTO_TCP, true, bind(socket_fd, ai->ai_addr, ai->ai_addrlen) == 0)
}


int net_sock_listen(const int socket_fd, const int backlog) {
    return listen(socket_fd, backlog);
}


int net_sock_accept(const int socket_fd) {
    struct sockaddr_storage client_addr;
    socklen_t addr_len = sizeof(client_addr);
    const auto client_fd = accept(socket_fd, (struct sockaddr*)&client_addr, &addr_len);
    return client_fd < 0 ? -1 : client_fd;
}


int net_sock_close(const int socket_fd) {
    if (socket_fd < 0) { return -1; }
    return close(socket_fd);
}


long long net_sock_sendto(const int socket_fd, unsigned char const *data, const size_t size, unsigned char const *restrict host, const unsigned short port) {
    if (socket_fd < 0 || data == nullptr || size == 0 || host == nullptr) { return -1; }
    _net_address_helper(SOCK_DGRAM, IPPROTO_UDP, false, sendto(socket_fd, data, size, 0, ai->ai_addr, ai->ai_addrlen) >= 0)
}


long long net_sock_recvfrom(const int socket_fd, unsigned char *buffer, const size_t size) {
    if (socket_fd < 0 || buffer == nullptr || size == 0) { return -1; }
    struct sockaddr_storage src_addr;
    socklen_t addr_len = sizeof(src_addr);
    return recvfrom(socket_fd, buffer, size, 0, (struct sockaddr*)&src_addr, &addr_len);
}


int net_sock_set_nonblocking(const int socket_fd, const bool non_blocking) {
    if (socket_fd < 0) { return -1; }
    auto flags = fcntl(socket_fd, F_GETFL, 0);
    flags = (flags & ~O_NONBLOCK) | (non_blocking ? O_NONBLOCK : 0);
    return fcntl(socket_fd, F_SETFL, flags);
}


int net_sock_set_recv_timeout(const int socket_fd, const int timeout_ms) {
    if (socket_fd < 0) { return -1; }
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    return setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
}


int net_sock_set_send_timeout(const int socket_fd, const int timeout_ms) {
    if (socket_fd < 0) { return -1; }
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    return setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
}


int net_sock_set_reuseaddr(const int socket_fd, const bool reuse) {
    if (socket_fd < 0) { return -1; }
    const auto optval = reuse ? 1 : 0;
    return setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof(optval));
}


int net_sock_set_keepalive(const int socket_fd, const bool keepalive) {
    if (socket_fd < 0) { return -1; }
    const auto optval = keepalive ? 1 : 0;
    return setsockopt(socket_fd, SOL_SOCKET, SO_KEEPALIVE, (const char*)&optval, sizeof(optval));
}


int net_sock_set_nodelay(const int socket_fd, const bool nodelay) {
    if (socket_fd < 0) { return -1; }
    const auto optval = nodelay ? 1 : 0;
    return setsockopt(socket_fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&optval, sizeof(optval));
}


long long time_now_ns(const int clock) {
    struct timespec ts;
    clock_gettime(clock, &ts);
    return (long long)ts.tv_sec * 1000000000 + (long long)ts.tv_nsec;
}


void time_sleep_ms(const long long milliseconds) {
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;
    nanosleep(&ts, nullptr);
}


void time_sleep_us(const long long microseconds) {
    struct timespec ts;
    ts.tv_sec = microseconds / 1000000;
    ts.tv_nsec = (microseconds % 1000000) * 1000;
    nanosleep(&ts, nullptr);
}


void time_sleep_ns(const long long nanoseconds) {
    struct timespec ts;
    ts.tv_sec = nanoseconds / 1000000000;
    ts.tv_nsec = nanoseconds % 1000000000;
    nanosleep(&ts, nullptr);
}


long long time_locl_tz_offset_seconds() {
    const auto now = time(nullptr);
    struct tm gmt_tm;
    struct tm local_tm;
    if (localtime_r(&now, &local_tm) == nullptr) { return -1; }
    if (gmtime_r(&now, &gmt_tm) == nullptr) { return -1; }
    return (long long)difftime(mktime(&local_tm), mktime(&gmt_tm));
}


int time_local_tz_name(unsigned char *restrict buffer, const size_t size) {
    if (buffer == nullptr || size == 0) { return -1; }
    const auto now = time(nullptr);
    struct tm local_tm;
    if (localtime_r(&now, &local_tm) == nullptr) { return -1; }
    if (strftime((char*)buffer, size, "%Z", &local_tm) == 0) { return -1; }
    return 0;
}


long long time_monotonic_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + (long long)ts.tv_nsec / 1000000;
}


long long time_monotonic_us() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000 + (long long)ts.tv_nsec / 1000;
}


long long time_monotonic_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000 + (long long)ts.tv_nsec;
}


long long time_performance_counter_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    return (long long)ts.tv_sec * 1000 + (long long)ts.tv_nsec / 1000000;
}


long long time_performance_counter_us() {
    struct timespec ts;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    return (long long)ts.tv_sec * 1000000 + (long long)ts.tv_nsec / 1000;
}


long long time_performance_counter_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    return (long long)ts.tv_sec * 1000000000 + (long long)ts.tv_nsec;
}


int proc_get_pid() {
    return getpid();
}


int proc_get_parent_pid() {
    return getppid();
}


int proc_set_env(unsigned char const *restrict key, unsigned char const *restrict value, const bool overwrite) {
    if (key == nullptr || value == nullptr) { return -1; }
    return setenv((const char*)key, (const char*)value, overwrite);
}


unsigned char* proc_get_env(unsigned char const *restrict key) {
    if (key == nullptr) { return nullptr; }
    return (unsigned char*)getenv((const char*)key);
}


int proc_unset_env(unsigned char const *restrict key) {
    if (key == nullptr) { return -1; }
    return unsetenv((const char*)key);
}


int proc_system(unsigned char const *restrict command) {
    if (command == nullptr) { return -1; }
    return system((const char*)command);
}


int proc_exec(unsigned char const *restrict path, unsigned char const *const *restrict argv, unsigned char const *const *restrict envp) {
    if (path == nullptr || argv == nullptr) { return -1; }
    const auto pid = fork();
    execve((const char*)path, (char* const*)argv, (char* const*)envp);
    return pid < 0 ? -1 : pid;
}


int proc_wait(const int pid) {
    if (pid < 0) { return -1; }
    auto status = 0;
    const auto ret = waitpid(pid, &status, 0);
    return ret < 0 ? -1 : status;
}


int proc_kill(const int pid) {
    if (pid < 0) { return -1; }
    return kill(pid, SIGKILL);
}


int proc_is_running(const int pid) {
    if (pid < 0) { return -1; }
    const auto ret = kill(pid, 0);
    return ret == 0 ? 1 : (errno == ESRCH ? 0 : -1);
}


void proc_get_cwd(unsigned char *restrict buffer, const size_t size) {
    return (void)getcwd((char*)buffer, size);
}


int proc_set_cwd(unsigned char const *restrict path) {
    if (path == nullptr) { return -1; }
    return chdir((const char*)path);
}


void rng_seed(const unsigned long long seed) {
    RNG_STATE = seed;
}


unsigned long long rng_next() {
    auto z = (RNG_STATE += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}


double rng_next_double() {
    return (rng_next() >> 11) * (1.0 / 9007199254740992.0);
}


void rng_fill_bytes(unsigned char *buffer, const size_t size) {
    if (buffer == nullptr || size == 0) { return; }
    for (size_t i = 0; i < size; ++i) {
        buffer[i] = (unsigned char)(rng_next() & 0xFF);
    }
}


int fs_exists(unsigned char const *restrict path) {
    if (path == nullptr) { return -1; }
    return access((const char*)path, F_OK) == 0 ? 1 : 0;
}


int fs_isfile(unsigned char const *restrict path) {
    if (path == nullptr) { return -1; }
    struct stat path_stat;
    if (stat((const char*)path, &path_stat) != 0) { return -1; }
    return S_ISREG(path_stat.st_mode) ? 1 : 0;
}


int fs_isdir(unsigned char const *restrict path) {
    if (path == nullptr) { return -1; }
    struct stat path_stat;
    if (stat((const char*)path, &path_stat) != 0) { return -1; }
    return S_ISDIR(path_stat.st_mode) ? 1 : 0;
}


int fs_issymlink(unsigned char const *restrict path) {
    if (path == nullptr) { return -1; }
    struct stat path_stat;
    if (lstat((const char*)path, &path_stat) != 0) { return -1; }
    return S_ISLNK(path_stat.st_mode) ? 1 : 0;
}


long long fs_file_size(unsigned char const *restrict path) {
    if (path == nullptr) { return -1; }
    struct stat path_stat;
    if (stat((const char*)path, &path_stat) != 0) { return -1; }
    return (long long)path_stat.st_size;
}


int fs_remove(unsigned char const *restrict path) {
    if (path == nullptr) { return -1; }
    return remove((const char*)path);
}


int fs_rename(unsigned char const *restrict old_path, unsigned char const *restrict new_path) {
    if (old_path == nullptr || new_path == nullptr) { return -1; }
    return rename((const char*)old_path, (const char*)new_path);
}


int fs_mkdir(unsigned char const *restrict path, const unsigned int mode) {
    if (path == nullptr) { return -1; }
    return mkdir((const char*)path, mode);
}


int fs_rmdir(unsigned char const *restrict path) {
    if (path == nullptr) { return -1; }
    return rmdir((const char*)path);
}


int fs_chmod(unsigned char const *restrict path, const unsigned int mode) {
    if (path == nullptr) { return -1; }
    return chmod((const char*)path, mode);
}


int fs_symlink_target(unsigned char const *restrict path, unsigned char *restrict buffer, const size_t size) {
    if (path == nullptr || buffer == nullptr || size == 0) { return -1; }
    const auto len = readlink((const char*)path, (char*)buffer, size - 1);
    if (len < 0) { return -1; }
    buffer[len] = '\0';
    return 0;
}


int sys_cpu_count() {
    return sysconf(_SC_NPROCESSORS_ONLN);
}


long long sys_page_size() {
    return sysconf(_SC_PAGESIZE);
}


unsigned long long sys_thread_id() {
    return pthread_self();
}


int thread_spawn(void*(*start_routine)(void *)) {
    if (start_routine == nullptr) { return -1; }
    pthread_t thread;
    const auto ret = pthread_create(&thread, nullptr, start_routine, nullptr);
    if (ret != 0) { return -1; }
    return (int)thread;
}


int thread_join(const int thread_id) {
    if (thread_id <= 0) { return -1; }
    const auto thread = (pthread_t)thread_id;
    return pthread_join(thread, nullptr);
}


int thread_detach(const int thread_id) {
    if (thread_id <= 0) { return -1; }
    const auto thread = (pthread_t)thread_id;
    return pthread_detach(thread);
}


int thread_kill(const int thread_id) {
    if (thread_id <= 0) { return -1; }
    const auto thread = (pthread_t)thread_id;
    return pthread_kill(thread, SIGKILL);
}


int thread_cancel(const int thread_id) {
    if (thread_id <= 0) { return -1; }
    const auto thread = (pthread_t)thread_id;
    return pthread_cancel(thread);
}


int thread_equal(const int thread_id_1, const int thread_id_2) {
    if (thread_id_1 <= 0 || thread_id_2 <= 0) { return -1; }
    const auto thread1 = (pthread_t)thread_id_1;
    const auto thread2 = (pthread_t)thread_id_2;
    return pthread_equal(thread1, thread2) ? 1 : 0;
}


int thread_yield() {
    return sched_yield();
}


int thread_sleep_ms(const long long milliseconds) {
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;
    return nanosleep(&ts, nullptr);
}


int thread_get_id() {
    return (int)pthread_self();
}


int mutex_create() {
    _thread_create_helper(MUTEX, mutex, mutex);
}


int mutex_lock(const int mutex_id) {
    if (mutex_id < 0 || (size_t)mutex_id >= MUTEX_CAP) { return -1; }
    return pthread_mutex_lock(&MUTEX_TABLE[mutex_id].primitive);
}


int mutex_trylock(const int mutex_id) {
    if (mutex_id < 0 || (size_t)mutex_id >= MUTEX_CAP) { return -1; }
    return pthread_mutex_trylock(&MUTEX_TABLE[mutex_id].primitive);
}


int mutex_unlock(const int mutex_id) {
    if (mutex_id < 0 || (size_t)mutex_id >= MUTEX_CAP) { return -1; }
    return pthread_mutex_unlock(&MUTEX_TABLE[mutex_id].primitive);
}


int mutex_destroy(const int mutex_id) {
    if (mutex_id < 0 || (size_t)mutex_id >= MUTEX_CAP) { return -1; }
    _thread_destroy_helper(MUTEX, mutex, mutex);
}


int condvar_create() {
    _thread_create_helper(COND, cond, cond);
}


int condvar_wait(const int condvar_id, const int mutex_id) {
    if (condvar_id < 0 || (size_t)condvar_id >= COND_CAP) { return -1; }
    if (mutex_id < 0 || (size_t)mutex_id >= MUTEX_CAP) { return -1; }

    const auto c = &COND_TABLE[condvar_id];
    const auto m = &MUTEX_TABLE[mutex_id];
    if (!c->in_use || !m->in_use) { return -1; }
    return pthread_cond_wait(&c->primitive, &m->primitive);
}


int condvar_signal(const int condvar_id) {
    if (condvar_id < 0 || (size_t)condvar_id >= COND_CAP) { return -1; }
    return pthread_cond_signal(&COND_TABLE[condvar_id].primitive);
}


int condvar_broadcast(const int condvar_id) {
    if (condvar_id < 0 || (size_t)condvar_id >= COND_CAP) { return -1; }
    return pthread_cond_broadcast(&COND_TABLE[condvar_id].primitive);
}


int condvar_destroy(const int condvar_id) {
    if (condvar_id < 0 || (size_t)condvar_id >= COND_CAP) { return -1; }
    pthread_mutex_lock(&COND_TABLE_MTX);
    pthread_cond_destroy(&COND_TABLE[condvar_id].primitive);
    COND_TABLE[condvar_id].in_use = false;
    pthread_mutex_unlock(&COND_TABLE_MTX);
    return 0;
}


int rwlock_create() {
    _thread_create_helper(RWLOCK, rwlock, rwlock);
}


int rwlock_rdlock(const int rwlock_id) {
    if (rwlock_id < 0 || (size_t)rwlock_id >= RWLOCK_CAP) { return -1; }
    return pthread_rwlock_rdlock(&RWLOCK_TABLE[rwlock_id].primitive);
}


int rwlock_wrlock(const int rwlock_id) {
    if (rwlock_id < 0 || (size_t)rwlock_id >= RWLOCK_CAP) { return -1; }
    return pthread_rwlock_wrlock(&RWLOCK_TABLE[rwlock_id].primitive);
}


int rwlock_tryrdlock(const int rwlock_id) {
    if (rwlock_id < 0 || (size_t)rwlock_id >= RWLOCK_CAP) { return -1; }
    return pthread_rwlock_tryrdlock(&RWLOCK_TABLE[rwlock_id].primitive);
}


int rwlock_trywrlock(const int rwlock_id) {
    if (rwlock_id < 0 || (size_t)rwlock_id >= RWLOCK_CAP) { return -1; }
    return pthread_rwlock_trywrlock(&RWLOCK_TABLE[rwlock_id].primitive);
}


int rwlock_unlock(const int rwlock_id) {
    if (rwlock_id < 0 || (size_t)rwlock_id >= RWLOCK_CAP) { return -1; }
    return pthread_rwlock_unlock(&RWLOCK_TABLE[rwlock_id].primitive);
}


int rwlock_destroy(const int rwlock_id) {
    if (rwlock_id < 0 || (size_t)rwlock_id >= RWLOCK_CAP) { return -1; }
    _thread_destroy_helper(RWLOCK, rwlock, rwlock);
}


int barrier_create(const int count) {
    _thread_create_helper(BARRIER, barrier, barrier, count);
}


int barrier_wait(const int barrier_id) {
    if (barrier_id < 0 || (size_t)barrier_id >= BARRIER_CAP) { return -1; }
    return pthread_barrier_wait(&BARRIER_TABLE[barrier_id].primitive);
}


int barrier_destroy(const int barrier_id) {
    if (barrier_id < 0 || (size_t)barrier_id >= BARRIER_CAP) { return -1; }
    _thread_destroy_helper(BARRIER, barrier, barrier);
}


int spinlock_create() {
    _thread_create_helper(SPINLOCK, spinlock, spin);
}


int spinlock_lock(const int spinlock_id) {
    if (spinlock_id < 0 || (size_t)spinlock_id >= SPINLOCK_CAP) { return -1; }
    return pthread_spin_lock(&SPINLOCK_TABLE[spinlock_id].primitive);
}


int spinlock_trylock(const int spinlock_id) {
    if (spinlock_id < 0 || (size_t)spinlock_id >= SPINLOCK_CAP) { return -1; }
    return pthread_spin_trylock(&SPINLOCK_TABLE[spinlock_id].primitive);
}


int spinlock_unlock(const int spinlock_id) {
    if (spinlock_id < 0 || (size_t)spinlock_id >= SPINLOCK_CAP) { return -1; }
    return pthread_spin_unlock(&SPINLOCK_TABLE[spinlock_id].primitive);
}


int spinlock_destroy(const int spinlock_id) {
    if (spinlock_id < 0 || (size_t)spinlock_id >= SPINLOCK_CAP) { return -1; }
    _thread_destroy_helper(SPINLOCK, spinlock, spin);
}
