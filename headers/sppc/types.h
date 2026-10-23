#pragma once

/// The types and constants of the public api. They belong to sppc, not to the
/// platform's libc: every platform sees the same layouts and the same values,
/// and each platform layer translates to and from its own. That is what lets
/// the FFI generator (tools/main.py) produce one binding for every target, so
/// the S++ standard library is written once.
///
/// The constant values are Linux x86_64's, so the numbers S++ already used
/// still hold - but they are frozen here, not read from a system header:
/// changing one is an ABI break. A value a platform has no equivalent for is
/// refused there with SPPC_EINVAL (or SPPC_ENOPROTOOPT for a socket option).
///
/// No struct here has hidden padding: the S++ side declares the same fields in
/// the same order, so every byte is a named field.

#include <stddef.h>
#include <stdint.h>

// ==================== STRUCTS ====================

/// A point in time or a span of it: whole seconds, and the nanoseconds past
/// them (below one billion).
typedef struct sppc_timespec {
  uint64_t sec;
  uint64_t nsec;
} sppc_timespec;

/// A span of time as SO_RCVTIMEO / SO_SNDTIMEO and "sppc_select" take it:
/// whole seconds, and the microseconds past them (below one million).
typedef struct sppc_timeval {
  uint64_t sec;
  uint64_t usec;
} sppc_timeval;

/// SO_LINGER's value: whether closing waits for unsent data, and for how many
/// seconds at most.
typedef struct sppc_linger {
  int32_t onoff;
  int32_t linger;
} sppc_linger;

/// What "sppc_stat", "sppc_fstat" and "sppc_lstat" report. "mode" is the file
/// type and permission bits (SPPC_S_*). Like POSIX's own, it is spelt with
/// "struct", as the function of the same name takes the plain name.
struct sppc_stat {
  uint64_t dev;
  uint64_t ino;
  uint64_t nlink;
  uint64_t rdev;
  int64_t size;
  int64_t block_size;
  int64_t blocks;
  sppc_timespec atim;
  sppc_timespec mtim;
  sppc_timespec ctim;
  uint32_t mode;
  uint32_t uid;
  uint32_t gid;
  uint32_t _reserved;
};

/// What "sppc_statvfs" reports. "flag" is the mount flags (SPPC_ST_*). Spelt
/// with "struct", as "struct sppc_stat" is.
struct sppc_statvfs {
  uint64_t block_size;
  uint64_t frag_size;
  uint64_t blocks;
  uint64_t blocks_free;
  uint64_t blocks_available;
  uint64_t files;
  uint64_t files_free;
  uint64_t files_available;
  uint64_t fs_id;
  uint64_t flag;
  uint64_t namemax;
};

/// One descriptor for "sppc_poll": the events to wait for (SPPC_POLL*), and
/// the ones that happened.
typedef struct sppc_pollfd {
  int32_t fd;
  int16_t events;
  int16_t revents;
} sppc_pollfd;

/// One buffer for "sppc_readv" / "sppc_writev".
typedef struct sppc_iovec {
  void *base;
  size_t len;
} sppc_iovec;

/// A set of descriptors for "sppc_select", below 1024: descriptor n is bit
/// (n % 64) of bits[n / 64].
typedef struct sppc_fd_set {
  uint64_t bits[16];
} sppc_fd_set;

/// A record lock for "sppc_fcntl_lock": its type (SPPC_F_RDLCK, _WRLCK,
/// _UNLCK), where "start" is measured from (SPPC_SEEK_*), and how many bytes
/// it covers (0 for all of the rest of the file). F_GETLK fills in the
/// process holding a conflicting lock. "type" and "whence" are full ints, so
/// the constants that fill them need no narrowing.
typedef struct sppc_flock {
  int64_t start;
  int64_t len;
  int32_t pid;
  int32_t type;
  int32_t whence;
  int32_t _reserved;
} sppc_flock;

/// A socket address, as the platform lays it out. It is only ever built and
/// read by sppc ("sppc_set_sockaddr_v4" and friends), so the bytes are opaque:
/// 128 of them, enough for any address family.
typedef struct sppc_sockaddr_storage {
  uint64_t _opaque[16];
} sppc_sockaddr_storage;

// ==================== ERRNO ====================

// Error numbers. Every "int" an sppc function returns is 0 or one of these.

#define SPPC_EPERM                    1
#define SPPC_ENOENT                   2
#define SPPC_ESRCH                    3
#define SPPC_EINTR                    4
#define SPPC_EIO                      5
#define SPPC_ENXIO                    6
#define SPPC_E2BIG                    7
#define SPPC_ENOEXEC                  8
#define SPPC_EBADF                    9
#define SPPC_ECHILD                   10
#define SPPC_EAGAIN                   11
#define SPPC_ENOMEM                   12
#define SPPC_EACCES                   13
#define SPPC_EFAULT                   14
#define SPPC_ENOTBLK                  15
#define SPPC_EBUSY                    16
#define SPPC_EEXIST                   17
#define SPPC_EXDEV                    18
#define SPPC_ENODEV                   19
#define SPPC_ENOTDIR                  20
#define SPPC_EISDIR                   21
#define SPPC_EINVAL                   22
#define SPPC_ENFILE                   23
#define SPPC_EMFILE                   24
#define SPPC_ENOTTY                   25
#define SPPC_ETXTBSY                  26
#define SPPC_EFBIG                    27
#define SPPC_ENOSPC                   28
#define SPPC_ESPIPE                   29
#define SPPC_EROFS                    30
#define SPPC_EMLINK                   31
#define SPPC_EPIPE                    32
#define SPPC_EDOM                     33
#define SPPC_ERANGE                   34
#define SPPC_EDEADLK                  35
#define SPPC_ENAMETOOLONG             36
#define SPPC_ENOLCK                   37
#define SPPC_ENOSYS                   38
#define SPPC_ENOTEMPTY                39
#define SPPC_ELOOP                    40
#define SPPC_EWOULDBLOCK              11
#define SPPC_ENOMSG                   42
#define SPPC_EIDRM                    43
#define SPPC_ECHRNG                   44
#define SPPC_EL2NSYNC                 45
#define SPPC_EL3HLT                   46
#define SPPC_EL3RST                   47
#define SPPC_ELNRNG                   48
#define SPPC_EUNATCH                  49
#define SPPC_ENOCSI                   50
#define SPPC_EL2HLT                   51
#define SPPC_EBADE                    52
#define SPPC_EBADR                    53
#define SPPC_EXFULL                   54
#define SPPC_ENOANO                   55
#define SPPC_EBADRQC                  56
#define SPPC_EBADSLT                  57
#define SPPC_EDEADLOCK                35
#define SPPC_EBFONT                   59
#define SPPC_ENOSTR                   60
#define SPPC_ENODATA                  61
#define SPPC_ETIME                    62
#define SPPC_ENOSR                    63
#define SPPC_ENONET                   64
#define SPPC_ENOPKG                   65
#define SPPC_EREMOTE                  66
#define SPPC_ENOLINK                  67
#define SPPC_EADV                     68
#define SPPC_ESRMNT                   69
#define SPPC_ECOMM                    70
#define SPPC_EPROTO                   71
#define SPPC_EMULTIHOP                72
#define SPPC_EDOTDOT                  73
#define SPPC_EBADMSG                  74
#define SPPC_EOVERFLOW                75
#define SPPC_ENOTUNIQ                 76
#define SPPC_EBADFD                   77
#define SPPC_EREMCHG                  78
#define SPPC_ELIBACC                  79
#define SPPC_ELIBBAD                  80
#define SPPC_ELIBSCN                  81
#define SPPC_ELIBMAX                  82
#define SPPC_ELIBEXEC                 83
#define SPPC_EILSEQ                   84
#define SPPC_ERESTART                 85
#define SPPC_ESTRPIPE                 86
#define SPPC_EUSERS                   87
#define SPPC_ENOTSOCK                 88
#define SPPC_EDESTADDRREQ             89
#define SPPC_EMSGSIZE                 90
#define SPPC_EPROTOTYPE               91
#define SPPC_ENOPROTOOPT              92
#define SPPC_EPROTONOSUPPORT          93
#define SPPC_ESOCKTNOSUPPORT          94
#define SPPC_EOPNOTSUPP               95
#define SPPC_EPFNOSUPPORT             96
#define SPPC_EAFNOSUPPORT             97
#define SPPC_EADDRINUSE               98
#define SPPC_EADDRNOTAVAIL            99
#define SPPC_ENETDOWN                 100
#define SPPC_ENETUNREACH              101
#define SPPC_ENETRESET                102
#define SPPC_ECONNABORTED             103
#define SPPC_ECONNRESET               104
#define SPPC_ENOBUFS                  105
#define SPPC_EISCONN                  106
#define SPPC_ENOTCONN                 107
#define SPPC_ESHUTDOWN                108
#define SPPC_ETOOMANYREFS             109
#define SPPC_ETIMEDOUT                110
#define SPPC_ECONNREFUSED             111
#define SPPC_EHOSTDOWN                112
#define SPPC_EHOSTUNREACH             113
#define SPPC_EALREADY                 114
#define SPPC_EINPROGRESS              115
#define SPPC_ESTALE                   116
#define SPPC_EUCLEAN                  117
#define SPPC_ENOTNAM                  118
#define SPPC_ENAVAIL                  119
#define SPPC_EISNAM                   120
#define SPPC_EREMOTEIO                121
#define SPPC_EDQUOT                   122
#define SPPC_ENOMEDIUM                123
#define SPPC_EMEDIUMTYPE              124
#define SPPC_ECANCELED                125
#define SPPC_ENOKEY                   126
#define SPPC_EKEYEXPIRED              127
#define SPPC_EKEYREVOKED              128
#define SPPC_EKEYREJECTED             129
#define SPPC_EOWNERDEAD               130
#define SPPC_ENOTRECOVERABLE          131
#define SPPC_ERFKILL                  132
#define SPPC_EHWPOISON                133

// ==================== OPEN ====================

// "sppc_open" flags, and the file status flags "sppc_fcntl_get"/"_set" read and
// write with F_GETFL/F_SETFL. The low two bits are the access mode (O_ACCMODE),
// a value rather than flags.

#define SPPC_O_ACCMODE                03
#define SPPC_O_RDONLY                 0
#define SPPC_O_WRONLY                 01
#define SPPC_O_RDWR                   02
#define SPPC_O_CREAT                  0100
#define SPPC_O_EXCL                   0200
#define SPPC_O_NOCTTY                 0400
#define SPPC_O_TRUNC                  01000
#define SPPC_O_APPEND                 02000
#define SPPC_O_NONBLOCK               04000
#define SPPC_O_DSYNC                  010000
#define SPPC_O_ASYNC                  020000
#define SPPC_O_DIRECT                 040000
#define SPPC_O_DIRECTORY              0200000
#define SPPC_O_NOFOLLOW               0400000
#define SPPC_O_NOATIME                01000000
#define SPPC_O_CLOEXEC                02000000
#define SPPC_O_SYNC                   04010000
#define SPPC_O_PATH                   010000000
#define SPPC_O_TMPFILE                020200000

// ==================== MODE ====================

// File type and permission bits: "sppc_stat"'s mode, and the mode "sppc_open",
// "sppc_mkdir" and "sppc_chmod" take.

#define SPPC_S_IFMT                   0170000
#define SPPC_S_IFSOCK                 0140000
#define SPPC_S_IFLNK                  0120000
#define SPPC_S_IFREG                  0100000
#define SPPC_S_IFBLK                  060000
#define SPPC_S_IFDIR                  040000
#define SPPC_S_IFCHR                  020000
#define SPPC_S_IFIFO                  010000
#define SPPC_S_ISUID                  04000
#define SPPC_S_ISGID                  02000
#define SPPC_S_ISVTX                  01000
#define SPPC_S_IRWXU                  0700
#define SPPC_S_IRUSR                  0400
#define SPPC_S_IWUSR                  0200
#define SPPC_S_IXUSR                  0100
#define SPPC_S_IRWXG                  070
#define SPPC_S_IRGRP                  040
#define SPPC_S_IWGRP                  020
#define SPPC_S_IXGRP                  010
#define SPPC_S_IRWXO                  07
#define SPPC_S_IROTH                  04
#define SPPC_S_IWOTH                  02
#define SPPC_S_IXOTH                  01

// ==================== ACCESS ====================

// "sppc_access" modes.

#define SPPC_F_OK                     0
#define SPPC_X_OK                     1
#define SPPC_W_OK                     2
#define SPPC_R_OK                     4

// ==================== SEEK ====================

// "sppc_lseek" origins.

#define SPPC_SEEK_SET                 0
#define SPPC_SEEK_CUR                 1
#define SPPC_SEEK_END                 2

// ==================== PROT ====================

// Memory protection: "sppc_mmap" and "sppc_memprotect".

#define SPPC_PROT_NONE                0x0
#define SPPC_PROT_READ                0x1
#define SPPC_PROT_WRITE               0x2
#define SPPC_PROT_EXEC                0x4

// ==================== MAP ====================

// "sppc_mmap" flags.

#define SPPC_MAP_SHARED               0x1
#define SPPC_MAP_PRIVATE              0x2
#define SPPC_MAP_FIXED                0x10
#define SPPC_MAP_ANONYMOUS            0x20
#define SPPC_MAP_NORESERVE            0x4000

// ==================== MSYNC ====================

// "sppc_msync" flags.

#define SPPC_MS_ASYNC                 0x1
#define SPPC_MS_INVALIDATE            0x2
#define SPPC_MS_SYNC                  0x4

// ==================== MADV ====================

// "sppc_madvise" advice.

#define SPPC_MADV_NORMAL              0
#define SPPC_MADV_RANDOM              1
#define SPPC_MADV_SEQUENTIAL          2
#define SPPC_MADV_WILLNEED            3
#define SPPC_MADV_DONTNEED            4
#define SPPC_MADV_FREE                8

// ==================== AF ====================

// Address families: "sppc_socket"'s domain, and what "sppc_sockaddr_family"
// reports.

#define SPPC_AF_UNSPEC                0
#define SPPC_AF_UNIX                  1
#define SPPC_AF_INET                  2
#define SPPC_AF_INET6                 10

// ==================== SOCK ====================

// Socket types, which "sppc_socket" also takes SOCK_NONBLOCK and SOCK_CLOEXEC
// or'd into.

#define SPPC_SOCK_STREAM              0x1
#define SPPC_SOCK_DGRAM               0x2
#define SPPC_SOCK_RAW                 0x3
#define SPPC_SOCK_SEQPACKET           0x5
#define SPPC_SOCK_NONBLOCK            0x800
#define SPPC_SOCK_CLOEXEC             0x80000

// ==================== IPPROTO ====================

// Protocols: "sppc_socket"'s protocol, and socket option levels.

#define SPPC_IPPROTO_IP               0
#define SPPC_IPPROTO_ICMP             1
#define SPPC_IPPROTO_TCP              6
#define SPPC_IPPROTO_UDP              17
#define SPPC_IPPROTO_IPV6             41
#define SPPC_IPPROTO_ICMPV6           58
#define SPPC_IPPROTO_RAW              255

// ==================== MSG ====================

// "sppc_send"/"sppc_recv" flags.

#define SPPC_MSG_OOB                  0x1
#define SPPC_MSG_PEEK                 0x2
#define SPPC_MSG_DONTROUTE            0x4
#define SPPC_MSG_TRUNC                0x20
#define SPPC_MSG_DONTWAIT             0x40
#define SPPC_MSG_WAITALL              0x100
#define SPPC_MSG_NOSIGNAL             0x4000

// ==================== SHUT ====================

// "sppc_shutdown" directions.

#define SPPC_SHUT_RD                  0
#define SPPC_SHUT_WR                  1
#define SPPC_SHUT_RDWR                2

// ==================== SOL ====================

// Socket option level for the socket itself.

#define SPPC_SOL_SOCKET               1

// ==================== SO ====================

// Socket options at SOL_SOCKET.

#define SPPC_SO_DEBUG                 1
#define SPPC_SO_REUSEADDR             2
#define SPPC_SO_TYPE                  3
#define SPPC_SO_ERROR                 4
#define SPPC_SO_DONTROUTE             5
#define SPPC_SO_BROADCAST             6
#define SPPC_SO_SNDBUF                7
#define SPPC_SO_RCVBUF                8
#define SPPC_SO_KEEPALIVE             9
#define SPPC_SO_OOBINLINE             10
#define SPPC_SO_LINGER                13
#define SPPC_SO_REUSEPORT             15
#define SPPC_SO_RCVLOWAT              18
#define SPPC_SO_SNDLOWAT              19
#define SPPC_SO_RCVTIMEO              20
#define SPPC_SO_SNDTIMEO              21
#define SPPC_SO_ACCEPTCONN            30

// ==================== TCP ====================

// Socket options at IPPROTO_TCP.

#define SPPC_TCP_NODELAY              1
#define SPPC_TCP_MAXSEG               2
#define SPPC_TCP_KEEPIDLE             4
#define SPPC_TCP_KEEPINTVL            5
#define SPPC_TCP_KEEPCNT              6

// ==================== IP ====================

// Socket options at IPPROTO_IP.

#define SPPC_IP_TOS                   1
#define SPPC_IP_TTL                   2
#define SPPC_IP_MULTICAST_TTL         33
#define SPPC_IP_MULTICAST_LOOP        34

// ==================== IPV6 ====================

// Socket options at IPPROTO_IPV6.

#define SPPC_IPV6_UNICAST_HOPS        16
#define SPPC_IPV6_MULTICAST_HOPS      18
#define SPPC_IPV6_MULTICAST_LOOP      19
#define SPPC_IPV6_V6ONLY              26

// ==================== FCNTL ====================

// "sppc_fcntl_get"/"_set"/"_lock" commands, the descriptor flag, and record lock
// types.

#define SPPC_F_GETFD                  1
#define SPPC_F_SETFD                  2
#define SPPC_F_GETFL                  3
#define SPPC_F_SETFL                  4
#define SPPC_F_GETLK                  5
#define SPPC_F_SETLK                  6
#define SPPC_F_SETLKW                 7
#define SPPC_F_SETOWN                 8
#define SPPC_F_GETOWN                 9
#define SPPC_FD_CLOEXEC               1
#define SPPC_F_RDLCK                  0
#define SPPC_F_WRLCK                  1
#define SPPC_F_UNLCK                  2

// ==================== POLL ====================

// "sppc_poll" events.

#define SPPC_POLLIN                   0x1
#define SPPC_POLLPRI                  0x2
#define SPPC_POLLOUT                  0x4
#define SPPC_POLLERR                  0x8
#define SPPC_POLLHUP                  0x10
#define SPPC_POLLNVAL                 0x20
#define SPPC_POLLRDNORM               0x40
#define SPPC_POLLRDBAND               0x80
#define SPPC_POLLWRNORM               0x100
#define SPPC_POLLWRBAND               0x200

// ==================== CLOCK ====================

// Clocks, and the absolute-deadline flag "sppc_clock_nanosleep" takes.

#define SPPC_CLOCK_REALTIME           0
#define SPPC_CLOCK_MONOTONIC          1
#define SPPC_CLOCK_PROCESS_CPUTIME_ID 2
#define SPPC_CLOCK_THREAD_CPUTIME_ID  3
#define SPPC_CLOCK_MONOTONIC_RAW      4
#define SPPC_CLOCK_REALTIME_COARSE    5
#define SPPC_CLOCK_MONOTONIC_COARSE   6
#define SPPC_CLOCK_BOOTTIME           7
#define SPPC_TIMER_ABSTIME            1

// ==================== SIG ====================

// Signals "sppc_signal" sends. 0 sends nothing and only checks the process
// exists.

#define SPPC_SIGHUP                   1
#define SPPC_SIGINT                   2
#define SPPC_SIGQUIT                  3
#define SPPC_SIGILL                   4
#define SPPC_SIGTRAP                  5
#define SPPC_SIGABRT                  6
#define SPPC_SIGBUS                   7
#define SPPC_SIGFPE                   8
#define SPPC_SIGKILL                  9
#define SPPC_SIGUSR1                  10
#define SPPC_SIGSEGV                  11
#define SPPC_SIGUSR2                  12
#define SPPC_SIGPIPE                  13
#define SPPC_SIGALRM                  14
#define SPPC_SIGTERM                  15
#define SPPC_SIGCHLD                  17
#define SPPC_SIGCONT                  18
#define SPPC_SIGSTOP                  19
#define SPPC_SIGTSTP                  20
#define SPPC_SIGTTIN                  21
#define SPPC_SIGTTOU                  22
#define SPPC_SIGURG                   23
#define SPPC_SIGXCPU                  24
#define SPPC_SIGXFSZ                  25
#define SPPC_SIGVTALRM                26
#define SPPC_SIGPROF                  27
#define SPPC_SIGWINCH                 28
#define SPPC_SIGIO                    29
#define SPPC_SIGSYS                   31

// ==================== AT ====================

// "sppc_utimensat" flags.

#define SPPC_AT_SYMLINK_NOFOLLOW      0x100

// ==================== ST ====================

// "sppc_statvfs" mount flags.

#define SPPC_ST_RDONLY                1
#define SPPC_ST_NOSUID                2
#define SPPC_ST_NODEV                 4
#define SPPC_ST_NOEXEC                8
#define SPPC_ST_SYNCHRONOUS           16
#define SPPC_ST_MANDLOCK              64
#define SPPC_ST_NOATIME               1024
#define SPPC_ST_NODIRATIME            2048
#define SPPC_ST_RELATIME              4096

// ==================== OTHER ====================

// What "sppc_pthread_barrier_wait" returns to exactly one of the threads it
// releases, rather than 0. Negative, so it can never be mistaken for an error.
#define SPPC_BARRIER_SERIAL_THREAD    (-1)
