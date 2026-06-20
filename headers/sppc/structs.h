#pragma once
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

/**
 * Helper struct for the "stat" family of functions, which return information about a file or stream. This is a
 * simplified version of the "struct stat" struct used in the POSIX API, containing only the most commonly used fields.
 * It provides a stable, non-os-specific interface for S++ to interact with file and stream metadata.
 */
typedef struct {
    uint64_t size; // file size in bytes
    uint64_t blocks; // blocks allocated
    uint32_t block_size; // preferred block size for I/O
    mode_t mode; // permissions + type flags
    uid_t uid; // owner user id
    gid_t gid; // owner group id
    uint64_t inode; // inode number
    uint64_t nlink; // number of hard links
    uint64_t atime; // last access time (seconds since epoch)
    uint64_t mtime; // last modification time (seconds since epoch)
    uint64_t ctime; // last status change time (seconds since epoch)
} fd_stat_t;

typedef struct {
    uint64_t total_bytes;
    uint64_t free_bytes;
    uint64_t available_bytes;
    uint64_t total_inodes;
    uint64_t free_inodes;
    uint64_t block_size;
    uint64_t max_filename_len;
} fs_statvfs_t;

typedef struct {
    int fd;
    short events;
    short revents;
} fd_poll_t;

typedef struct {
    void *buffer;
    size_t length;
} fd_iovec_t;

typedef struct {
    void *addr;
    size_t length;
} fd_mmap_t;

typedef struct {
    char *path;
    int fd;
} fs_temp_t;

typedef struct {
    int wd;
    uint32_t mask;
    uint32_t cookie;
    char *name;
} fs_watch_event_t;

typedef struct {
    fs_watch_event_t *events;
    size_t count;
} fs_watch_events_t;

typedef struct {
    char host[46]; // null terminated and enough for ipv6
    uint16_t port;
    int family;
} so_addr_t;

typedef struct {
    int32_t year;
    int32_t month;
    int32_t day;
    int32_t hour;
    int32_t minute;
    int32_t second;
    int32_t weekday;
    int32_t yearday;
    int32_t is_dst;
    int32_t tz_offset; // seconds east of UTC
} ti_breakdown_t;

typedef struct {
    time_t seconds;
    time_t nanoseconds;
} ti_duration_t;

typedef struct {
    int32_t exit_code;
    int32_t signal;
    bool exited;
    bool signaled;
    bool stopped;
} pr_wait_result_t;

typedef struct {
    char *key;
    char *val;
} pr_env_var_t;

typedef struct {
    pr_env_var_t *vars;
    size_t count;
} pr_env_t;

typedef struct {
    char *sysname;
    char *nodename;
    char *domainname;
    char *release;
    char *version;
    char *machine;
} sys_uname_t;

typedef struct {
    double one;
    double five;
    double ten;
} sys_loadavg_t;

typedef struct {
    uint64_t total;
    uint64_t free;
    uint64_t shared;
    uint64_t buffered;
    uint64_t swap_total;
    uint64_t swap_free;
    uint64_t uptime_seconds;
    uint16_t procs;
} sys_info_t;

typedef struct {
    uint64_t soft;
    uint64_t hard;
} sys_rlimit_t;
