#pragma once

/// Helpers every platform's own code uses, ahead of its api: read by each
/// platform's file (platform/posix.h, platform/windows.h) once its system
/// headers are in, as they name the platform's constants (AF_INET, POLLIN,
/// ...), each guarded where some platform lacks it. Not the api itself: the
/// parts of that every platform shares are platform/common.h's, read last.

#include <sppc/config.h>
#include <sppc/macros.h>
#include <sppc/types.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// ==================== TRANSLATION ====================

// The public api speaks sppc's types and constants (sppc/types.h); these turn
// them into the platform's and back. A family of constants whose values every
// POSIX system shares passes straight through, with the claim checked at
// compile time; any other goes through a small table of {sppc, platform}
// pairs, so nothing assumes a value - not even on Linux, where the numbers are
// mostly the same but O_DIRECT, O_DIRECTORY and friends differ between x86_64
// and arm64. A value the platform has no equivalent for is refused rather than
// guessed at, and one coming back that sppc has no name for is dropped.

typedef struct {
  int sppc;
  int os;
} _sppc_pair;

#define _SPPC_PAIR(name) {SPPC_##name, name}
#define _SPPC_COUNT(table) (sizeof (table) / sizeof *(table))

/// One value of a set (a clock, a command, ...) to the platform's.
_gnu_inline _gnu_nonnull(2, 4)
int _sppc_to_os_value(const int value, _sppc_pair const *table, const size_t n, int *_sppc_restrict out) {
  for (size_t i = 0; i < n; ++i) {
    if (table[i].sppc == value) {
      *out = table[i].os;
      return 0;
    }
  }
  return SPPC_EINVAL;
}

/// One of the platform's values back to sppc's, or "fallback" if it has none.
_gnu_inline _gnu_nonnull(2)
int _sppc_from_os_value(const int value, _sppc_pair const *table, const size_t n, const int fallback) {
  for (size_t i = 0; i < n; ++i) {
    if (table[i].os == value) { return table[i].sppc; }
  }
  return fallback;
}

/// Flags to the platform's. A flag made of several bits (O_SYNC includes
/// O_DSYNC's) is listed before the flags it contains, so it is taken whole;
/// a bit no entry claims is refused.
_gnu_inline _gnu_nonnull(2, 4)
int _sppc_to_os_flags(int flags, _sppc_pair const *table, const size_t n, int *_sppc_restrict out) {
  int os = 0;
  for (size_t i = 0; i < n && flags != 0; ++i) {
    if (table[i].sppc != 0 && (flags & table[i].sppc) == table[i].sppc) {
      os |= table[i].os;
      flags &= ~table[i].sppc;
    }
  }
  if (flags != 0) { return SPPC_EINVAL; }
  *out = os;
  return 0;
}

/// The platform's flags back to sppc's, dropping any sppc has no name for.
_gnu_inline _gnu_nonnull(2)
int _sppc_from_os_flags(int flags, _sppc_pair const *table, const size_t n) {
  int out = 0;
  for (size_t i = 0; i < n && flags != 0; ++i) {
    if (table[i].os != 0 && (flags & table[i].os) == table[i].os) {
      out |= table[i].sppc;
      flags &= ~table[i].os;
    }
  }
  return out;
}

/// Whether a table maps every value to itself, so a whole array of them can be
/// handed to the platform untouched.
_gnu_inline _gnu_nonnull(1)
bool _sppc_table_is_identity(_sppc_pair const *table, const size_t n) {
  for (size_t i = 0; i < n; ++i) {
    if (table[i].sppc != table[i].os) { return false; }
  }
  return true;
}

// ==================== MEMORY ====================

/// Every PROT_* bit sppc names: mmap and memprotect refuse any other.
#define _SPPC_PROT_ALL (SPPC_PROT_READ | SPPC_PROT_WRITE | SPPC_PROT_EXEC)

// ==================== SOCKETS ====================

// Only ever handed to the platform as room for an address, with the
// platform's own length, so it need only be as large and as aligned:
// m68k's is 126 bytes, not 128.
_Static_assert(sizeof(sppc_sockaddr_storage) >= sizeof(struct sockaddr_storage)
  && _Alignof(sppc_sockaddr_storage) >= _Alignof(struct sockaddr_storage), "sppc: sockaddr_storage differs");

/// The platform's view of an address only sppc ever reads or writes.
#define _sppc_os_sockaddr(storage) ((struct sockaddr_storage*)(storage))
#define _sppc_os_sockaddr_const(storage) ((struct sockaddr_storage const*)(storage))

/// How much of a storage the address in it occupies, by its family.
_gnu_inline _gnu_nonnull(1)
socklen_t _sppc_sockaddr_len(struct sockaddr_storage const *storage) {
  return storage->ss_family == AF_INET ? (socklen_t)sizeof(struct sockaddr_in) : (socklen_t)sizeof(struct sockaddr_in6);
}

_gnu_inline
_sppc_pair const* _sppc_af_table(size_t *_sppc_restrict n) {
  static const _sppc_pair table[] = {
    _SPPC_PAIR(AF_UNSPEC), _SPPC_PAIR(AF_UNIX),
    _SPPC_PAIR(AF_INET), _SPPC_PAIR(AF_INET6)
  };
  *n = _SPPC_COUNT(table);
  return table;
}

_gnu_inline
_sppc_pair const* _sppc_sock_types_table(size_t *_sppc_restrict n) {
  static const _sppc_pair table[] = {
    _SPPC_PAIR(SOCK_STREAM), _SPPC_PAIR(SOCK_DGRAM),
    _SPPC_PAIR(SOCK_RAW), _SPPC_PAIR(SOCK_SEQPACKET),
  };
  *n = _SPPC_COUNT(table);
  return table;
}

// Darwin's "socket" and "socketpair" take neither flag. Two bits no socket
// type uses stand in for them, and "_sppc_os_socket" applies them with
// "fcntl" once the socket exists.
#if defined(SOCK_NONBLOCK)
  #define _SPPC_OS_SOCK_NONBLOCK SOCK_NONBLOCK
  #define _SPPC_OS_SOCK_CLOEXEC SOCK_CLOEXEC
  #define _SPPC_SOCK_FLAGS_BY_FCNTL 0
#else
  #define _SPPC_OS_SOCK_NONBLOCK 0x10000000
  #define _SPPC_OS_SOCK_CLOEXEC 0x20000000
  #define _SPPC_SOCK_FLAGS_BY_FCNTL 1
#endif

/// A socket type, with SOCK_NONBLOCK / SOCK_CLOEXEC or'd in.
_gnu_inline _gnu_nonnull(2)
int _sppc_to_os_sock_type(const int type, int *_sppc_restrict out) {
  static const _sppc_pair flags[] = {
    {SPPC_SOCK_NONBLOCK, _SPPC_OS_SOCK_NONBLOCK}, {SPPC_SOCK_CLOEXEC, _SPPC_OS_SOCK_CLOEXEC},
  };
  const int sppc_flags = type & (SPPC_SOCK_NONBLOCK | SPPC_SOCK_CLOEXEC);
  size_t n;
  _sppc_pair const *const types = _sppc_sock_types_table(&n);
  int os_type, os_flags;
  if (_sppc_to_os_value(type & ~sppc_flags, types, n, &os_type) != 0) { return SPPC_EINVAL; }
  if (_sppc_to_os_flags(sppc_flags, flags, _SPPC_COUNT(flags), &os_flags) != 0) { return SPPC_EINVAL; }
  *out = os_type | os_flags;
  return 0;
}

_gnu_inline _gnu_nonnull(2)
int _sppc_to_os_msg_flags(const int flags, int *_sppc_restrict out) {
  static const _sppc_pair table[] = {
    _SPPC_PAIR(MSG_OOB), _SPPC_PAIR(MSG_PEEK), _SPPC_PAIR(MSG_DONTROUTE), _SPPC_PAIR(MSG_WAITALL),
#ifdef MSG_TRUNC
    _SPPC_PAIR(MSG_TRUNC),
#endif
#ifdef MSG_DONTWAIT
    _SPPC_PAIR(MSG_DONTWAIT),
#endif
#ifdef MSG_NOSIGNAL
    _SPPC_PAIR(MSG_NOSIGNAL),
#endif
  };
  if (_sppc_likely(flags == 0)) {
    *out = 0;
    return 0;
  }
  return _sppc_to_os_flags(flags, table, _SPPC_COUNT(table), out);
}

/// A socket option: its level and name together, as a name only means
/// something at its own level.
_gnu_inline _gnu_nonnull(3, 4)
int _sppc_to_os_sockopt(const int level, const int name, int *_sppc_restrict out_level, int *_sppc_restrict out_name) {
  static const _sppc_pair socket[] = {
    _SPPC_PAIR(SO_DEBUG), _SPPC_PAIR(SO_REUSEADDR), _SPPC_PAIR(SO_TYPE), _SPPC_PAIR(SO_ERROR),
    _SPPC_PAIR(SO_DONTROUTE), _SPPC_PAIR(SO_BROADCAST), _SPPC_PAIR(SO_SNDBUF), _SPPC_PAIR(SO_RCVBUF),
    _SPPC_PAIR(SO_KEEPALIVE), _SPPC_PAIR(SO_OOBINLINE), _SPPC_PAIR(SO_LINGER),
    _SPPC_PAIR(SO_RCVLOWAT), _SPPC_PAIR(SO_SNDLOWAT), _SPPC_PAIR(SO_RCVTIMEO), _SPPC_PAIR(SO_SNDTIMEO),
    _SPPC_PAIR(SO_ACCEPTCONN),
#ifdef SO_REUSEPORT
    _SPPC_PAIR(SO_REUSEPORT),
#endif
  };
  static const _sppc_pair tcp[] = {
    _SPPC_PAIR(TCP_NODELAY), _SPPC_PAIR(TCP_MAXSEG),
#ifdef TCP_KEEPIDLE
    _SPPC_PAIR(TCP_KEEPIDLE),
#elif defined(TCP_KEEPALIVE)
    // Darwin's name for it: idle seconds before the first probe.
    {SPPC_TCP_KEEPIDLE, TCP_KEEPALIVE},
#endif
#ifdef TCP_KEEPINTVL
    _SPPC_PAIR(TCP_KEEPINTVL),
#endif
#ifdef TCP_KEEPCNT
    _SPPC_PAIR(TCP_KEEPCNT),
#endif
  };
  static const _sppc_pair ip[] = {
    _SPPC_PAIR(IP_TOS), _SPPC_PAIR(IP_TTL), _SPPC_PAIR(IP_MULTICAST_TTL), _SPPC_PAIR(IP_MULTICAST_LOOP),
  };
  static const _sppc_pair ipv6[] = {
    _SPPC_PAIR(IPV6_UNICAST_HOPS), _SPPC_PAIR(IPV6_MULTICAST_HOPS), _SPPC_PAIR(IPV6_MULTICAST_LOOP),
    _SPPC_PAIR(IPV6_V6ONLY),
  };

  _sppc_pair const *table;
  size_t n;
  int os_level;
  switch (level) {
    case SPPC_SOL_SOCKET: table = socket; n = _SPPC_COUNT(socket); os_level = SOL_SOCKET; break;
    case SPPC_IPPROTO_TCP: table = tcp; n = _SPPC_COUNT(tcp); os_level = IPPROTO_TCP; break;
    case SPPC_IPPROTO_IP: table = ip; n = _SPPC_COUNT(ip); os_level = IPPROTO_IP; break;
    case SPPC_IPPROTO_IPV6: table = ipv6; n = _SPPC_COUNT(ipv6); os_level = IPPROTO_IPV6; break;
    default: return SPPC_ENOPROTOOPT;
  }
  if (_sppc_to_os_value(name, table, n, out_name) != 0) { return SPPC_ENOPROTOOPT; }
  *out_level = os_level;
  return 0;
}

// ==================== POLL ====================

_gnu_inline
_sppc_pair const* _sppc_poll_table(size_t *_sppc_restrict n) {
  static const _sppc_pair table[] = {
    _SPPC_PAIR(POLLIN), _SPPC_PAIR(POLLPRI), _SPPC_PAIR(POLLOUT), _SPPC_PAIR(POLLERR), _SPPC_PAIR(POLLHUP),
    _SPPC_PAIR(POLLNVAL), _SPPC_PAIR(POLLRDNORM), _SPPC_PAIR(POLLRDBAND), _SPPC_PAIR(POLLWRNORM),
    _SPPC_PAIR(POLLWRBAND),
  };
  *n = _SPPC_COUNT(table);
  return table;
}

// ==================== TEMPORARY NAMES ====================

/// Whether a template ends in the six X's the name is made from. glibc
/// insists on them; Darwin takes fewer, even none, and makes the path as is.
_gnu_inline _gnu_nonnull(1)
bool _sppc_is_temp_template(char const *path) {
  const size_t len = strlen(path);
  return len >= 6 && memcmp(path + len - 6, "XXXXXX", 6) == 0;
}

// ==================== ARGUMENTS ====================

// The program's arguments: kept by each platform's "sppc_set_args", set once
// before any of the program's own code runs, and only read after (the getters
// are platform/common.h's).
extern size_t _sppc_nargs;
extern char const *const *_sppc_args;
