#include <sppc/sppc.h>
#include <assert.h>

// sppc's own types and constants (sppc/types.h) and their translation to the
// platform's. First every table, constant by constant, both ways; then values
// the platform must refuse; then the public functions end to end, so a struct
// copied field by field or an array passed straight through is seen working.

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

// ==================== TABLES ====================

#define TO_OS(fn, name) do { int os_ = -12345; CHECK(fn(SPPC_##name, &os_) == 0 && os_ == (name)); } while (0)

static void check_open_flags(void) {
#define OPEN(name) do { \
    int os_ = -1; CHECK(_sppc_to_os_open_flags(SPPC_##name, &os_) == 0 && os_ == (name)); \
    CHECK(_sppc_from_os_open_flags(name) == SPPC_##name); } while (0)
  OPEN(O_RDONLY); OPEN(O_WRONLY); OPEN(O_RDWR); OPEN(O_CREAT); OPEN(O_EXCL); OPEN(O_NOCTTY); OPEN(O_TRUNC);
  OPEN(O_APPEND); OPEN(O_NONBLOCK); OPEN(O_DSYNC); OPEN(O_ASYNC); OPEN(O_DIRECTORY);
  OPEN(O_NOFOLLOW); OPEN(O_CLOEXEC); OPEN(O_SYNC);
#if SPPC_KERNEL_LINUX
  OPEN(O_DIRECT); OPEN(O_NOATIME); OPEN(O_PATH); OPEN(O_TMPFILE);
#else
  // Linux's own, which nothing else can honour, so they are refused.
  int refused = 0;
  CHECK(_sppc_to_os_open_flags(SPPC_O_DIRECT, &refused) == SPPC_EINVAL);
  CHECK(_sppc_to_os_open_flags(SPPC_O_NOATIME, &refused) == SPPC_EINVAL);
  CHECK(_sppc_to_os_open_flags(SPPC_O_PATH, &refused) == SPPC_EINVAL);
  CHECK(_sppc_to_os_open_flags(SPPC_O_TMPFILE, &refused) == SPPC_EINVAL);
#endif
#undef OPEN
  // Several at once, including a multi-bit flag that contains another.
  int os = 0;
  CHECK(_sppc_to_os_open_flags(SPPC_O_RDWR | SPPC_O_CREAT | SPPC_O_SYNC | SPPC_O_CLOEXEC, &os) == 0);
  CHECK(os == (O_RDWR | O_CREAT | O_SYNC | O_CLOEXEC));
  CHECK(_sppc_from_os_open_flags(O_WRONLY | O_APPEND | O_NONBLOCK) == (SPPC_O_WRONLY | SPPC_O_APPEND | SPPC_O_NONBLOCK));
}

static void check_values(void) {
  TO_OS(_sppc_to_os_map_flags, MAP_SHARED); TO_OS(_sppc_to_os_map_flags, MAP_PRIVATE);
  TO_OS(_sppc_to_os_map_flags, MAP_FIXED); TO_OS(_sppc_to_os_map_flags, MAP_ANONYMOUS);
  TO_OS(_sppc_to_os_map_flags, MAP_NORESERVE);
  TO_OS(_sppc_to_os_msync_flags, MS_ASYNC); TO_OS(_sppc_to_os_msync_flags, MS_INVALIDATE);
  TO_OS(_sppc_to_os_msync_flags, MS_SYNC);
  TO_OS(_sppc_to_os_madvise, MADV_NORMAL); TO_OS(_sppc_to_os_madvise, MADV_RANDOM);
  TO_OS(_sppc_to_os_madvise, MADV_SEQUENTIAL); TO_OS(_sppc_to_os_madvise, MADV_WILLNEED);
  TO_OS(_sppc_to_os_madvise, MADV_DONTNEED); TO_OS(_sppc_to_os_madvise, MADV_FREE);
  TO_OS(_sppc_to_os_sock_type, SOCK_STREAM); TO_OS(_sppc_to_os_sock_type, SOCK_DGRAM);
  TO_OS(_sppc_to_os_sock_type, SOCK_RAW); TO_OS(_sppc_to_os_sock_type, SOCK_SEQPACKET);
  int os = 0;
  CHECK(_sppc_to_os_sock_type(SPPC_SOCK_STREAM | SPPC_SOCK_NONBLOCK | SPPC_SOCK_CLOEXEC, &os) == 0);
  CHECK(os == (SOCK_STREAM | _SPPC_OS_SOCK_NONBLOCK | _SPPC_OS_SOCK_CLOEXEC));
  TO_OS(_sppc_to_os_msg_flags, MSG_OOB); TO_OS(_sppc_to_os_msg_flags, MSG_PEEK);
  TO_OS(_sppc_to_os_msg_flags, MSG_DONTROUTE); TO_OS(_sppc_to_os_msg_flags, MSG_TRUNC);
  TO_OS(_sppc_to_os_msg_flags, MSG_DONTWAIT); TO_OS(_sppc_to_os_msg_flags, MSG_WAITALL);
  TO_OS(_sppc_to_os_msg_flags, MSG_NOSIGNAL);
  TO_OS(_sppc_to_os_timer_flags, TIMER_ABSTIME);
  TO_OS(_sppc_to_os_at_flags, AT_SYMLINK_NOFOLLOW);
  TO_OS(_sppc_to_os_signal, SIGHUP); TO_OS(_sppc_to_os_signal, SIGINT); TO_OS(_sppc_to_os_signal, SIGKILL);
  TO_OS(_sppc_to_os_signal, SIGTERM); TO_OS(_sppc_to_os_signal, SIGUSR1); TO_OS(_sppc_to_os_signal, SIGCHLD);
  TO_OS(_sppc_to_os_signal, SIGSYS); TO_OS(_sppc_to_os_signal, SIGWINCH); TO_OS(_sppc_to_os_signal, SIGIO);

#define CLOCK(name) do { clockid_t c_ = (clockid_t)-1; CHECK(_sppc_to_os_clock(SPPC_##name, &c_) == 0 && c_ == (name)); } while (0)
  CLOCK(CLOCK_REALTIME); CLOCK(CLOCK_MONOTONIC); CLOCK(CLOCK_PROCESS_CPUTIME_ID); CLOCK(CLOCK_THREAD_CPUTIME_ID);
  CLOCK(CLOCK_MONOTONIC_RAW);
#if SPPC_KERNEL_LINUX
  CLOCK(CLOCK_REALTIME_COARSE); CLOCK(CLOCK_MONOTONIC_COARSE); CLOCK(CLOCK_BOOTTIME);
#endif
#undef CLOCK

#define SOCKOPT(level, name) do { int l_ = -1, n_ = -1; \
    CHECK(_sppc_to_os_sockopt(SPPC_##level, SPPC_##name, &l_, &n_) == 0 && l_ == (level) && n_ == (name)); } while (0)
  SOCKOPT(SOL_SOCKET, SO_REUSEADDR); SOCKOPT(SOL_SOCKET, SO_TYPE); SOCKOPT(SOL_SOCKET, SO_ERROR);
  SOCKOPT(SOL_SOCKET, SO_KEEPALIVE); SOCKOPT(SOL_SOCKET, SO_LINGER); SOCKOPT(SOL_SOCKET, SO_BROADCAST);
  SOCKOPT(SOL_SOCKET, SO_RCVTIMEO); SOCKOPT(SOL_SOCKET, SO_SNDTIMEO); SOCKOPT(SOL_SOCKET, SO_REUSEPORT);
  SOCKOPT(SOL_SOCKET, SO_SNDBUF); SOCKOPT(SOL_SOCKET, SO_RCVBUF); SOCKOPT(SOL_SOCKET, SO_ACCEPTCONN);
  SOCKOPT(IPPROTO_TCP, TCP_NODELAY); SOCKOPT(IPPROTO_TCP, TCP_MAXSEG);
#if defined(TCP_KEEPIDLE)
  SOCKOPT(IPPROTO_TCP, TCP_KEEPIDLE);
#else
  { int l_ = -1, n_ = -1;
    CHECK(_sppc_to_os_sockopt(SPPC_IPPROTO_TCP, SPPC_TCP_KEEPIDLE, &l_, &n_) == 0 && n_ == TCP_KEEPALIVE); }
#endif
  SOCKOPT(IPPROTO_TCP, TCP_KEEPINTVL); SOCKOPT(IPPROTO_TCP, TCP_KEEPCNT);
  SOCKOPT(IPPROTO_IP, IP_TOS); SOCKOPT(IPPROTO_IP, IP_TTL); SOCKOPT(IPPROTO_IP, IP_MULTICAST_TTL);
  SOCKOPT(IPPROTO_IPV6, IPV6_V6ONLY); SOCKOPT(IPPROTO_IPV6, IPV6_UNICAST_HOPS);
#undef SOCKOPT

  size_t n;
  _sppc_pair const *t = _sppc_af_table(&n);
  int af = -1;
  CHECK(_sppc_to_os_value(SPPC_AF_INET6, t, n, &af) == 0 && af == AF_INET6);
  CHECK(_sppc_from_os_value(AF_UNIX, t, n, -1) == SPPC_AF_UNIX);
  t = _sppc_poll_table(&n);
  CHECK(_sppc_from_os_flags(POLLIN | POLLHUP, t, n) == (SPPC_POLLIN | SPPC_POLLHUP));
  t = _sppc_lock_types_table(&n);
  int lk = -1;
  CHECK(_sppc_to_os_value(SPPC_F_WRLCK, t, n, &lk) == 0 && lk == F_WRLCK);

  CHECK(_sppc_errno(ENOENT) == SPPC_ENOENT && _sppc_errno(ETIMEDOUT) == SPPC_ETIMEDOUT);
  CHECK(_sppc_errno(0) == 0);
#if SPPC_KERNEL_DARWIN
  // Every Darwin number becomes an sppc one, and none of them 0: the ones
  // with a counterpart exactly, and the rest SPPC_EIO.
  for (int e = 1; e <= ELAST; ++e) {
    const int s = _sppc_errno(e);
    CHECK(s > 0 && s <= SPPC_EHWPOISON);
  }
  CHECK(_sppc_errno(EAGAIN) == SPPC_EAGAIN && _sppc_errno(EWOULDBLOCK) == SPPC_EAGAIN);
  CHECK(_sppc_errno(EDEADLK) == SPPC_EDEADLK && _sppc_errno(EINPROGRESS) == SPPC_EINPROGRESS);
  CHECK(_sppc_errno(ECONNREFUSED) == SPPC_ECONNREFUSED && _sppc_errno(EOWNERDEAD) == SPPC_EOWNERDEAD);
  CHECK(_sppc_errno(ENOTSUP) == SPPC_EOPNOTSUPP && _sppc_errno(EOPNOTSUPP) == SPPC_EOPNOTSUPP);
  CHECK(_sppc_errno(ENOATTR) == SPPC_ENODATA && _sppc_errno(EBADARCH) == SPPC_ENOEXEC);
  CHECK(_sppc_errno(EAUTH) == SPPC_EIO && _sppc_errno(ELAST + 1) == SPPC_EIO && _sppc_errno(-1) == SPPC_EIO);
#endif
}

// ==================== REFUSALS ====================

static void check_refusals(void) {
  int os = 0;
  CHECK(_sppc_to_os_open_flags(SPPC_O_ACCMODE, &os) == SPPC_EINVAL);       // 3 is no access mode
  CHECK(_sppc_to_os_open_flags(SPPC_O_RDONLY | 0x40000000, &os) == SPPC_EINVAL); // a bit sppc has no name for
  CHECK(_sppc_to_os_sock_type(99, &os) == SPPC_EINVAL);
  CHECK(_sppc_to_os_signal(99, &os) == SPPC_EINVAL);
  clockid_t c;
  CHECK(_sppc_to_os_clock(99, &c) == SPPC_EINVAL);
  int l, nm;
  CHECK(_sppc_to_os_sockopt(SPPC_SOL_SOCKET, 9999, &l, &nm) == SPPC_ENOPROTOOPT);
  CHECK(_sppc_to_os_sockopt(12345, SPPC_SO_TYPE, &l, &nm) == SPPC_ENOPROTOOPT);
  mode_t m;
  CHECK(_sppc_to_os_mode(SPPC_S_IFREG | 0644, &m) == SPPC_EINVAL);           // only permission bits go in

  const sppc_timespec bad_ns = {1, 1000000000u};
  struct timespec ts;
  CHECK(_sppc_to_os_timespec(&bad_ns, &ts) == SPPC_EINVAL);
  CHECK(sppc_clock_nanosleep(SPPC_CLOCK_MONOTONIC, 0, &bad_ns) == SPPC_EINVAL);
  int fd = 0;
  CHECK(sppc_open("/", 0x40000000, 0, &fd) == SPPC_EINVAL && fd == -1);
  int64_t pos = 0;
  CHECK(sppc_lseek(0, 0, 7, &pos) == SPPC_EINVAL);
  CHECK(sppc_signal((int32_t)getpid(), 99) == SPPC_EINVAL);
  CHECK(sppc_shutdown(0, 9) == SPPC_EINVAL);
}

// ==================== END TO END ====================

static void check_files(void) {
  char path[] = "/tmp/sppc_types_XXXXXX";
  int fd = -1;
  CHECK(sppc_mktemp(path, &fd) == 0 && fd >= 0);
  ssize_t n = 0;
  CHECK(sppc_write("hello", 1, 5, fd, &n) == 0 && n == 5);

  // A stat copied field by field must agree with the platform's own.
  struct sppc_stat st;
  struct stat os_st;
  CHECK(sppc_fstat(fd, &st) == 0 && fstat(fd, &os_st) == 0);
  CHECK(st.size == 5 && st.ino == (uint64_t)os_st.st_ino && st.dev == (uint64_t)os_st.st_dev);
  CHECK((st.mode & SPPC_S_IFMT) == SPPC_S_IFREG && (st.mode & 07777) == (os_st.st_mode & 07777));
#if SPPC_KERNEL_DARWIN
  struct timespec const os_mtim = os_st.st_mtimespec;
#else
  struct timespec const os_mtim = os_st.st_mtim;
#endif
  CHECK(st.mtim.sec == (uint64_t)os_mtim.tv_sec && st.mtim.nsec == (uint64_t)os_mtim.tv_nsec);
  CHECK(st.nlink == 1 && st.uid == (uint32_t)os_st.st_uid && st.block_size == (int64_t)os_st.st_blksize);
  struct sppc_stat by_path;
  CHECK(sppc_stat(path, &by_path) == 0 && by_path.ino == st.ino);

  struct sppc_statvfs vfs;
  CHECK(sppc_statvfs("/tmp", &vfs) == 0 && vfs.block_size > 0 && vfs.namemax > 0);

  // F_GETFL comes back as sppc's flags, and F_SETFL takes them.
  int fl = 0;
  CHECK(sppc_fcntl_get(fd, SPPC_F_GETFL, &fl) == 0 && (fl & SPPC_O_ACCMODE) == SPPC_O_RDWR);
  CHECK(sppc_fcntl_set(fd, SPPC_F_SETFL, fl | SPPC_O_NONBLOCK) == 0);
  CHECK(sppc_fcntl_get(fd, SPPC_F_GETFL, &fl) == 0 && (fl & SPPC_O_NONBLOCK));
  CHECK(sppc_fcntl_get(fd, SPPC_F_GETFD, &fl) == 0 && (fl & SPPC_FD_CLOEXEC));
  CHECK(sppc_fcntl_get(fd, 9999, &fl) == SPPC_EINVAL);

  // A record lock taken, and seen by F_GETLK as no conflict for its owner.
  sppc_flock lock = {.start = 0, .len = 0, .pid = 0, .type = SPPC_F_WRLCK, .whence = SPPC_SEEK_SET};
  CHECK(sppc_fcntl_lock(fd, SPPC_F_SETLK, &lock) == 0);
  sppc_flock probe = {.start = 0, .len = 0, .pid = 0, .type = SPPC_F_RDLCK, .whence = SPPC_SEEK_SET};
  CHECK(sppc_fcntl_lock(fd, SPPC_F_GETLK, &probe) == 0 && probe.type == SPPC_F_UNLCK);
  lock.type = SPPC_F_UNLCK;
  CHECK(sppc_fcntl_lock(fd, SPPC_F_SETLK, &lock) == 0);

  // readv through the platform's own iovec layout.
  int64_t pos = -1;
  CHECK(sppc_lseek(fd, 0, SPPC_SEEK_SET, &pos) == 0 && pos == 0);
  char a[2] = {0}, b[3] = {0};
  sppc_iovec iov[2] = {{a, 2}, {b, 3}};
  CHECK(sppc_readv(fd, iov, 2, &n) == 0 && n == 5 && a[0] == 'h' && b[2] == 'o');

  int access_rc = -1;
  CHECK(sppc_access(path, SPPC_R_OK | SPPC_W_OK, &access_rc) == 0 && access_rc == 0);
  CHECK(sppc_chmod(path, 0600) == 0 && sppc_stat(path, &by_path) == 0 && (by_path.mode & 0777) == 0600);

  // An error comes back as sppc's number.
  int missing = 0;
  CHECK(sppc_open("/nonexistent/sppc", SPPC_O_RDONLY, 0, &missing) == SPPC_ENOENT);

  CHECK(sppc_close(fd) == 0);
  CHECK(sppc_unlink(path) == 0);
}

static void check_poll_and_select(void) {
  int r = -1, w = -1;
  CHECK(sppc_pipe(&r, &w) == 0);
  ssize_t n = 0;
  CHECK(sppc_write("x", 1, 1, w, &n) == 0);

  sppc_pollfd fds[2] = {{.fd = r, .events = SPPC_POLLIN, .revents = 0}, {.fd = w, .events = SPPC_POLLOUT, .revents = 0}};
  int ready = -1;
  CHECK(sppc_poll(fds, 2, 0, &ready) == 0 && ready == 2);
  CHECK((fds[0].revents & SPPC_POLLIN) && (fds[1].revents & SPPC_POLLOUT));
  CHECK(fds[0].events == SPPC_POLLIN); // the caller's events are left as they were

  // The path a platform with different event values takes.
  fds[0].revents = fds[1].revents = 0;
  CHECK(_sppc_poll_translated(fds, 2, 0, &ready) == 0 && ready == 2);
  CHECK((fds[0].revents & SPPC_POLLIN) && (fds[1].revents & SPPC_POLLOUT));
  fds[0].events = 0x4000; // no name
  CHECK(sppc_poll(fds, 2, 0, &ready) == SPPC_EINVAL);

  sppc_fd_set readable = {{0}};
  readable.bits[r / 64] |= (uint64_t)1 << (r % 64);
  sppc_timeval tv = {0, 1000};
  CHECK(sppc_select(r + 1, &readable, NULL, NULL, &tv, &ready) == 0 && ready == 1);
  CHECK(readable.bits[r / 64] & ((uint64_t)1 << (r % 64)));
  CHECK(sppc_select(5000, &readable, NULL, NULL, &tv, &ready) == SPPC_EINVAL);

  sppc_close(r);
  sppc_close(w);
}

static void check_sockets(void) {
  int fd = -1;
  CHECK(sppc_socket(SPPC_AF_INET, SPPC_SOCK_STREAM | SPPC_SOCK_CLOEXEC, SPPC_IPPROTO_TCP, &fd) == 0 && fd >= 0);

  int yes = 1, value = -1;
  CHECK(sppc_setsockopt(fd, SPPC_SOL_SOCKET, SPPC_SO_REUSEADDR, &yes) == 0);
  CHECK(sppc_getsockopt(fd, SPPC_SOL_SOCKET, SPPC_SO_REUSEADDR, &value) == 0 && value != 0);
  CHECK(sppc_getsockopt(fd, SPPC_SOL_SOCKET, SPPC_SO_TYPE, &value) == 0 && value == SPPC_SOCK_STREAM);
  CHECK(sppc_getsockopt(fd, SPPC_SOL_SOCKET, SPPC_SO_ERROR, &value) == 0 && value == 0);
  CHECK(sppc_setsockopt(fd, SPPC_IPPROTO_TCP, SPPC_TCP_NODELAY, &yes) == 0);
  CHECK(sppc_setsockopt(fd, SPPC_SOL_SOCKET, 9999, &yes) == SPPC_ENOPROTOOPT);

  const sppc_linger lg = {1, 7};
  sppc_linger lg_back = {0, 0};
  CHECK(sppc_setsockopt_linger(fd, SPPC_SOL_SOCKET, SPPC_SO_LINGER, &lg) == 0);
  CHECK(sppc_getsockopt_linger(fd, SPPC_SOL_SOCKET, SPPC_SO_LINGER, &lg_back) == 0);
  CHECK(lg_back.onoff != 0 && lg_back.linger == 7);

  const sppc_timeval tv = {2, 500000};
  sppc_timeval tv_back = {0, 0};
  CHECK(sppc_setsockopt_timeval(fd, SPPC_SOL_SOCKET, SPPC_SO_RCVTIMEO, &tv) == 0);
  CHECK(sppc_getsockopt_timeval(fd, SPPC_SOL_SOCKET, SPPC_SO_RCVTIMEO, &tv_back) == 0);
  CHECK(tv_back.sec == 2 && tv_back.usec == 500000);

  sppc_sockaddr_storage addr;
  const uint8_t loopback[4] = {127, 0, 0, 1};
  sppc_set_sockaddr_v4(loopback, 0, &addr);
  int family = -1;
  sppc_sockaddr_family(&addr, &family);
  CHECK(family == SPPC_AF_INET);
  CHECK(sppc_bind(fd, &addr) == 0);
  sppc_sockaddr_storage bound;
  CHECK(sppc_getsockname(fd, &bound) == 0);
  uint8_t octets[4];
  uint16_t port = 0;
  sppc_get_sockaddr_v4(&bound, octets, &port);
  CHECK(octets[0] == 127 && port != 0);

  CHECK(sppc_socket(12345, SPPC_SOCK_STREAM, 0, &value) == SPPC_EINVAL);
  sppc_close(fd);
}

static void check_misc(void) {
  sppc_timespec now = {0, 0};
  CHECK(sppc_clock_gettime(SPPC_CLOCK_REALTIME, &now) == 0 && now.sec > 1700000000u && now.nsec < 1000000000u);
  CHECK(sppc_clock_gettime(99, &now) == SPPC_EINVAL);
  CHECK(sppc_signal((int32_t)getpid(), 0) == 0); // 0: only checks the process exists

  // A one-thread barrier releases its only waiter as the serial thread, which
  // must never read as an error.
  uint64_t barrier = 0;
  CHECK(sppc_pthread_barrier_init(&barrier, 1) == 0);
  CHECK(sppc_pthread_barrier_wait(&barrier) == SPPC_BARRIER_SERIAL_THREAD);
  CHECK(sppc_pthread_barrier_destroy(&barrier) == 0);
}

int main(void) {
  assert(sppc_init() == 0);
  check_open_flags();
  check_values();
  check_refusals();
  check_files();
  check_poll_and_select();
  check_sockets();
  check_misc();
  assert(sppc_cleanup() == 0);
  printf("%s (%d failures)\n", failures == 0 ? "ok" : "FAILED", failures);
  return failures == 0 ? 0 : 1;
}
