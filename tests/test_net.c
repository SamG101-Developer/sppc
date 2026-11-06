#include "check.h"

// Sockets over loopback: TCP and UDP, IPv4 and IPv6, on the calling thread
// and between two tasks of the async runtime (where accept, connect, send and
// recv park the task rather than the thread). Addresses go in and come back
// through sppc's own helpers, segments and ports in host order.

static const uint8_t v4_loopback[4] = {127, 0, 0, 1};
static const uint16_t v6_loopback[8] = {0, 0, 0, 0, 0, 0, 0, 1};

static int have_v6 = 0;

static void sleep_ms(const uint64_t ms) {
  const sppc_timespec d = {0, ms * 1000000};
  sppc_clock_nanosleep(SPPC_CLOCK_MONOTONIC, 0, &d);
}

/// A listening TCP socket on an ephemeral loopback port, and that port.
static int listener(const int family, uint16_t *out_port) {
  int fd = -1;
  if (sppc_socket(family, SPPC_SOCK_STREAM | SPPC_SOCK_CLOEXEC, 0, &fd) != 0) { return -1; }
  sppc_sockaddr_storage addr, bound;
  if (family == SPPC_AF_INET) { sppc_set_sockaddr_v4(v4_loopback, 0, &addr); }
  else { sppc_set_sockaddr_v6(v6_loopback, 0, &addr); }
  CHECK(sppc_bind(fd, &addr) == 0);
  CHECK(sppc_listen(fd, 8) == 0);
  CHECK(sppc_getsockname(fd, &bound) == 0);
  if (family == SPPC_AF_INET) {
    uint8_t octets[4];
    sppc_get_sockaddr_v4(&bound, octets, out_port);
  }
  else {
    uint16_t segments[8];
    sppc_get_sockaddr_v6(&bound, segments, out_port);
    CHECK(memcmp(segments, v6_loopback, sizeof segments) == 0);
  }
  return fd;
}

static void loopback_addr(const int family, const uint16_t port, sppc_sockaddr_storage *out) {
  if (family == SPPC_AF_INET) { sppc_set_sockaddr_v4(v4_loopback, port, out); }
  else { sppc_set_sockaddr_v6(v6_loopback, port, out); }
}

/// Whether two addresses are the same family, address and port. Only those:
/// the rest of the storage is whatever was in it before.
static int same_addr(sppc_sockaddr_storage const *a, sppc_sockaddr_storage const *b) {
  int fa = -1, fb = -2;
  sppc_sockaddr_family(a, &fa);
  sppc_sockaddr_family(b, &fb);
  if (fa != fb) { return 0; }
  uint16_t pa = 0, pb = 1;
  if (fa == SPPC_AF_INET) {
    uint8_t oa[4], ob[4];
    sppc_get_sockaddr_v4(a, oa, &pa);
    sppc_get_sockaddr_v4(b, ob, &pb);
    return pa == pb && memcmp(oa, ob, sizeof oa) == 0;
  }
  uint16_t sa[8], sb[8];
  sppc_get_sockaddr_v6(a, sa, &pa);
  sppc_get_sockaddr_v6(b, sb, &pb);
  return pa == pb && memcmp(sa, sb, sizeof sa) == 0;
}

// ==================== ADDRESSES ====================

static void check_addresses(void) {
  // Segments go in and come out in host order; the bytes on the wire are
  // big-endian, which the platform's own view of the address confirms.
  const uint16_t in[8] = {0x2001, 0x0db8, 0x0001, 0x0203, 0x0405, 0x0607, 0x0809, 0xabcd};
  sppc_sockaddr_storage storage;
  sppc_set_sockaddr_v6(in, 8080, &storage);
  uint16_t out[8];
  uint16_t port = 0;
  sppc_get_sockaddr_v6(&storage, out, &port);
  CHECK(memcmp(in, out, sizeof in) == 0 && port == 8080);
  int family = -1;
  sppc_sockaddr_family(&storage, &family);
  CHECK(family == SPPC_AF_INET6);

  struct sockaddr_in6 const *os = (struct sockaddr_in6 const*)&storage;
  CHECK(os->sin6_addr.s6_addr[0] == 0x20 && os->sin6_addr.s6_addr[1] == 0x01);
  CHECK(os->sin6_addr.s6_addr[14] == 0xab && os->sin6_addr.s6_addr[15] == 0xcd);
  CHECK(ntohs(os->sin6_port) == 8080);

  sppc_set_sockaddr_v6(v6_loopback, 1, &storage);
  CHECK(IN6_IS_ADDR_LOOPBACK(&os->sin6_addr));

  const uint8_t octets[4] = {10, 1, 2, 3};
  sppc_set_sockaddr_v4(octets, 65535, &storage);
  uint8_t back[4];
  sppc_get_sockaddr_v4(&storage, back, &port);
  CHECK(memcmp(octets, back, 4) == 0 && port == 65535);
  sppc_sockaddr_family(&storage, &family);
  CHECK(family == SPPC_AF_INET);
}

// ==================== TCP ====================

static void check_tcp(const int family) {
  uint16_t port = 0;
  const int lfd = listener(family, &port);
  CHECK(lfd >= 0 && port != 0);

  // The connection completes into the backlog, so one thread can do both ends.
  int client = -1, server = -1;
  sppc_sockaddr_storage addr, peer, client_name, server_peer;
  loopback_addr(family, port, &addr);
  CHECK(sppc_socket(family, SPPC_SOCK_STREAM, 0, &client) == 0);
  CHECK(sppc_connect(client, &addr) == 0);
  CHECK(sppc_accept(lfd, &peer, &server) == 0 && server >= 0);

  // Each end names the other.
  CHECK(sppc_getsockname(client, &client_name) == 0);
  CHECK(sppc_getpeername(server, &server_peer) == 0);
  CHECK(same_addr(&client_name, &server_peer));
  CHECK(same_addr(&client_name, &peer)); // and accept said so too
  sppc_sockaddr_storage client_peer;
  CHECK(sppc_getpeername(client, &client_peer) == 0);
  uint16_t peer_port = 0;
  if (family == SPPC_AF_INET) {
    uint8_t o[4];
    sppc_get_sockaddr_v4(&client_peer, o, &peer_port);
  }
  else {
    uint16_t s[8];
    sppc_get_sockaddr_v6(&client_peer, s, &peer_port);
  }
  CHECK(peer_port == port);
  CHECK(sppc_getpeername(lfd, &client_peer) == SPPC_ENOTCONN);

  // Data both ways; a peek leaves it there.
  ssize_t n = 0;
  char buf[32] = {0};
  CHECK(sppc_send(client, "ping", 4, 0, &n) == 0 && n == 4);
  CHECK(sppc_recv(server, buf, 2, SPPC_MSG_PEEK, &n) == 0 && n == 2 && memcmp(buf, "pi", 2) == 0);
  CHECK(sppc_recv(server, buf, 4, SPPC_MSG_WAITALL, &n) == 0 && n == 4 && memcmp(buf, "ping", 4) == 0);
  CHECK(sppc_send(server, "pong", 4, SPPC_MSG_NOSIGNAL, &n) == 0 && n == 4);
  CHECK(sppc_read(buf, 1, sizeof buf, client, &n) == 0 && n == 4 && memcmp(buf, "pong", 4) == 0);

  // Nothing waiting: a non-blocking receive says so.
  CHECK(sppc_recv(server, buf, sizeof buf, SPPC_MSG_DONTWAIT, &n) == SPPC_EAGAIN && n == -1);
  CHECK(sppc_recv(server, buf, sizeof buf, 0x40000000, &n) == SPPC_EINVAL);

  // Shutting the write side is an end of stream to the reader.
  CHECK(sppc_shutdown(client, SPPC_SHUT_WR) == 0);
  CHECK(sppc_recv(server, buf, sizeof buf, 0, &n) == 0 && n == 0);

  // Once the peer has gone, writing fails rather than raising SIGPIPE.
  CHECK(sppc_close(client) == 0);
  int rc = 0;
  // The peer's reset arrives after the first write, and not always at once.
  for (int i = 0; i < 100 && rc == 0; ++i) {
    rc = sppc_send(server, "x", 1, 0, &n);
    if (rc == 0) { sleep_ms(1); }
  }
  CHECK(rc == SPPC_EPIPE || rc == SPPC_ECONNRESET);
  if (rc != SPPC_EPIPE && rc != SPPC_ECONNRESET) { printf("  send after close: %d\n", rc); }

  sppc_close(server);

  // The port is taken while the listener holds it.
  int again = -1;
  CHECK(sppc_socket(family, SPPC_SOCK_STREAM, 0, &again) == 0);
  loopback_addr(family, port, &addr);
  CHECK(sppc_bind(again, &addr) == SPPC_EADDRINUSE);
  sppc_close(again);

  // And refuses connections once it is closed.
  sppc_close(lfd);
  CHECK(sppc_socket(family, SPPC_SOCK_STREAM, 0, &again) == 0);
  CHECK(sppc_connect(again, &addr) == SPPC_ECONNREFUSED);
  sppc_close(again);

  // Errors on the wrong kind of descriptor.
  int pr = -1, pw = -1;
  CHECK(sppc_pipe(&pr, &pw) == 0);
  CHECK(sppc_listen(pr, 1) == SPPC_ENOTSOCK);
  CHECK(sppc_send(pw, "x", 1, 0, &n) == SPPC_ENOTSOCK);
  sppc_close(pr);
  sppc_close(pw);
  CHECK(sppc_accept(-1, &peer, &server) == SPPC_EBADF && server == -1);
}

// ==================== UDP ====================

static void check_udp(const int family) {
  int a = -1, b = -1;
  CHECK(sppc_socket(family, SPPC_SOCK_DGRAM | SPPC_SOCK_CLOEXEC, 0, &a) == 0);
  CHECK(sppc_socket(family, SPPC_SOCK_DGRAM | SPPC_SOCK_CLOEXEC, 0, &b) == 0);
  sppc_sockaddr_storage any, a_name, b_name, from;
  loopback_addr(family, 0, &any);
  CHECK(sppc_bind(a, &any) == 0 && sppc_bind(b, &any) == 0);
  CHECK(sppc_getsockname(a, &a_name) == 0 && sppc_getsockname(b, &b_name) == 0);

  // Each datagram arrives whole, with its sender's address.
  ssize_t n = 0;
  CHECK(sppc_sendto(a, "first", 5, &b_name, &n) == 0 && n == 5);
  CHECK(sppc_sendto(a, "second!", 7, &b_name, &n) == 0 && n == 7);
  char buf[32];
  CHECK(sppc_recvfrom(b, buf, sizeof buf, &from, &n) == 0 && n == 5 && memcmp(buf, "first", 5) == 0);
  CHECK(same_addr(&from, &a_name));
  // A buffer too small for the datagram gets its start; the rest is lost.
  CHECK(sppc_recvfrom(b, buf, 3, &from, &n) == 0 && n == 3 && memcmp(buf, "sec", 3) == 0);
  CHECK(sppc_recv(b, buf, sizeof buf, SPPC_MSG_DONTWAIT, &n) == SPPC_EAGAIN);

  // A connected datagram socket can use send and recv.
  CHECK(sppc_connect(a, &b_name) == 0);
  CHECK(sppc_send(a, "conn", 4, 0, &n) == 0 && n == 4);
  CHECK(sppc_recv(b, buf, sizeof buf, 0, &n) == 0 && n == 4 && memcmp(buf, "conn", 4) == 0);

  sppc_close(a);
  sppc_close(b);
}

// ==================== OPTIONS ====================

static void check_options(void) {
  int fd = -1, v = -1;
  CHECK(sppc_socket(SPPC_AF_INET, SPPC_SOCK_STREAM, 0, &fd) == 0);

  // What is set comes back, through sppc's levels and names.
  const int on = 1;
  CHECK(sppc_setsockopt(fd, SPPC_SOL_SOCKET, SPPC_SO_REUSEADDR, &on) == 0);
  CHECK(sppc_getsockopt(fd, SPPC_SOL_SOCKET, SPPC_SO_REUSEADDR, &v) == 0 && v != 0);
  CHECK(sppc_setsockopt(fd, SPPC_IPPROTO_TCP, SPPC_TCP_NODELAY, &on) == 0);
  CHECK(sppc_getsockopt(fd, SPPC_IPPROTO_TCP, SPPC_TCP_NODELAY, &v) == 0 && v != 0);
  CHECK(sppc_getsockopt(fd, SPPC_SOL_SOCKET, SPPC_SO_TYPE, &v) == 0 && v == SPPC_SOCK_STREAM);
  CHECK(sppc_getsockopt(fd, SPPC_SOL_SOCKET, SPPC_SO_ERROR, &v) == 0 && v == 0);

  const sppc_timeval wait = {2, 500000};
  sppc_timeval back = {0, 0};
  CHECK(sppc_setsockopt_timeval(fd, SPPC_SOL_SOCKET, SPPC_SO_RCVTIMEO, &wait) == 0);
  CHECK(sppc_getsockopt_timeval(fd, SPPC_SOL_SOCKET, SPPC_SO_RCVTIMEO, &back) == 0 && back.sec == 2 && back.usec == 500000);
  const sppc_timeval bad = {0, 1000000};
  CHECK(sppc_setsockopt_timeval(fd, SPPC_SOL_SOCKET, SPPC_SO_SNDTIMEO, &bad) == SPPC_EINVAL);

  const sppc_linger linger = {1, 5};
  sppc_linger got = {0, 0};
  CHECK(sppc_setsockopt_linger(fd, SPPC_SOL_SOCKET, SPPC_SO_LINGER, &linger) == 0);
  CHECK(sppc_getsockopt_linger(fd, SPPC_SOL_SOCKET, SPPC_SO_LINGER, &got) == 0 && got.onoff != 0 && got.linger == 5);

  // Names sppc has no translation for, and descriptors that are no socket.
  CHECK(sppc_getsockopt(fd, SPPC_SOL_SOCKET, 0x7777, &v) == SPPC_ENOPROTOOPT);
  CHECK(sppc_getsockopt(fd, 0x7777, SPPC_SO_TYPE, &v) == SPPC_ENOPROTOOPT);
  int pr = -1, pw = -1;
  CHECK(sppc_pipe(&pr, &pw) == 0);
  CHECK(sppc_getsockopt(pr, SPPC_SOL_SOCKET, SPPC_SO_TYPE, &v) == SPPC_ENOTSOCK);
  sppc_close(pr);
  sppc_close(pw);

  // A socket is a socket to fstat.
  struct sppc_stat st;
  CHECK(sppc_fstat(fd, &st) == 0 && (st.mode & SPPC_S_IFMT) == SPPC_S_IFSOCK);
  sppc_close(fd);
}

// ==================== READINESS ====================

static void check_readiness(void) {
  uint16_t port = 0;
  const int lfd = listener(SPPC_AF_INET, &port);
  int client = -1, server = -1, n = -1;
  sppc_sockaddr_storage addr, peer;
  loopback_addr(SPPC_AF_INET, port, &addr);

  // Nothing waiting, nothing ready.
  sppc_pollfd one = {lfd, SPPC_POLLIN, 0};
  CHECK(sppc_poll(&one, 1, 0, &n) == 0 && n == 0 && one.revents == 0);

  // A connection waiting makes the listener readable; a connected socket is
  // writable, and readable once there is data.
  CHECK(sppc_socket(SPPC_AF_INET, SPPC_SOCK_STREAM, 0, &client) == 0);
  CHECK(sppc_connect(client, &addr) == 0);
  CHECK(sppc_poll(&one, 1, 1000, &n) == 0 && n == 1 && (one.revents & SPPC_POLLIN));
  CHECK(sppc_accept(lfd, &peer, &server) == 0);
  ssize_t sent = 0;
  CHECK(sppc_write("data", 1, 4, client, &sent) == 0 && sent == 4); // write works on a socket too
  sppc_pollfd both[3] = {{server, SPPC_POLLIN, 0}, {client, SPPC_POLLOUT, 0}, {-1, SPPC_POLLIN, 0}};
  CHECK(sppc_poll(both, 3, 1000, &n) == 0 && n == 2);
  CHECK((both[0].revents & SPPC_POLLIN) && (both[1].revents & SPPC_POLLOUT) && both[2].revents == 0);

  // A pipe beside the sockets, and a descriptor that is not open.
  int pr = -1, pw = -1;
  CHECK(sppc_pipe(&pr, &pw) == 0);
  sppc_pollfd mixed[3] = {{pr, SPPC_POLLIN, 0}, {client, SPPC_POLLIN, 0}, {9999, SPPC_POLLIN, 0}};
  CHECK(sppc_poll(mixed, 2, 0, &n) == 0 && n == 0);
  CHECK(sppc_write("p", 1, 1, pw, &sent) == 0);
  CHECK(sppc_poll(mixed, 3, 1000, &n) == 0 && n == 2);
  CHECK((mixed[0].revents & SPPC_POLLIN) && mixed[1].revents == 0 && (mixed[2].revents & SPPC_POLLNVAL));
  CHECK(sppc_poll(mixed, 1, 0, &n) == 0 && n == 1); // still there: poll does not take it
  sppc_close(pr);
  sppc_close(pw);

  // select: the same, through descriptor sets.
  sppc_fd_set rd, wr;
  memset(&rd, 0, sizeof rd);
  memset(&wr, 0, sizeof wr);
  rd.bits[server / 64] |= (uint64_t)1 << (server % 64);
  wr.bits[client / 64] |= (uint64_t)1 << (client % 64);
  sppc_timeval tv = {1, 0};
  const int top = (server > client ? server : client) + 1;
  CHECK(sppc_select(top, &rd, &wr, NULL, &tv, &n) == 0 && n == 2);
  CHECK((rd.bits[server / 64] >> (server % 64) & 1u) && (wr.bits[client / 64] >> (client % 64) & 1u));
  char buf[8];
  ssize_t got = 0;
  CHECK(sppc_read(buf, 1, sizeof buf, server, &got) == 0 && got == 4 && memcmp(buf, "data", 4) == 0);
  memset(&rd, 0, sizeof rd);
  rd.bits[server / 64] |= (uint64_t)1 << (server % 64);
  tv = (sppc_timeval){0, 20000};
  CHECK(sppc_select(top, &rd, NULL, NULL, &tv, &n) == 0 && n == 0 && rd.bits[server / 64] == 0);
  CHECK(sppc_select(-1, NULL, NULL, NULL, NULL, &n) == SPPC_EINVAL);

  // O_NONBLOCK through fcntl: a receive, read or accept with nothing there says so.
  int flags = 0;
  CHECK(sppc_fcntl_get(server, SPPC_F_GETFL, &flags) == 0);
  CHECK(sppc_fcntl_set(server, SPPC_F_SETFL, flags | SPPC_O_NONBLOCK) == 0);
  CHECK(sppc_recv(server, buf, sizeof buf, 0, &got) == SPPC_EAGAIN);
  CHECK(sppc_read(buf, 1, sizeof buf, server, &got) == SPPC_EAGAIN);
  CHECK(sppc_fcntl_set(server, SPPC_F_SETFL, flags) == 0);
  int accepted = -1;
  CHECK(sppc_fcntl_get(lfd, SPPC_F_GETFL, &flags) == 0);
  CHECK(sppc_fcntl_set(lfd, SPPC_F_SETFL, flags | SPPC_O_NONBLOCK) == 0);
  CHECK(sppc_accept(lfd, &peer, &accepted) == SPPC_EAGAIN && accepted == -1);

  // The peer's end of stream is readable.
  CHECK(sppc_shutdown(client, SPPC_SHUT_WR) == 0);
  one = (sppc_pollfd){server, SPPC_POLLIN, 0};
  CHECK(sppc_poll(&one, 1, 1000, &n) == 0 && n == 1 && (one.revents & (SPPC_POLLIN | SPPC_POLLHUP)));
  CHECK(sppc_recv(server, buf, sizeof buf, 0, &got) == 0 && got == 0);

  sppc_close(server);
  sppc_close(client);
  sppc_close(lfd);
}

// ==================== ASYNC ====================

// A server task and a client task on one thread. The server's accept and
// receive would block the only thread there is, so each has to park its task
// and let the client run. The state is static: a task's environment belongs
// to the runtime, which frees it when the task ends.

static struct { int lfd; char got[16]; ssize_t n; } server;
static struct { int family; uint16_t port; } client;
static char order[16];
static int order_n = 0;

static void server_task(void *e) {
  (void)e;
  int fd = -1;
  sppc_sockaddr_storage peer;
  order[order_n++] = 'A';
  CHECK(sppc_accept(server.lfd, &peer, &fd) == 0);
  CHECK(sppc_recv(fd, server.got, 11, SPPC_MSG_WAITALL, &server.n) == 0);
  ssize_t n = 0;
  CHECK(sppc_send(fd, "ack", 3, 0, &n) == 0 && n == 3);
  order[order_n++] = 'a';
  sppc_close(fd);
}

static void client_task(void *e) {
  (void)e;
  order[order_n++] = 'C';
  int fd = -1;
  sppc_sockaddr_storage addr;
  loopback_addr(client.family, client.port, &addr);
  CHECK(sppc_socket(client.family, SPPC_SOCK_STREAM, 0, &fd) == 0);
  CHECK(sppc_connect(fd, &addr) == 0);
  ssize_t n = 0;
  // In two pieces with a yield between, so the server's WAITALL receive has
  // to wait for the second.
  CHECK(sppc_send(fd, "hello ", 6, 0, &n) == 0);
  sppc_async_yield();
  CHECK(sppc_send(fd, "world", 5, 0, &n) == 0);
  char buf[8] = {0};
  CHECK(sppc_recv(fd, buf, sizeof buf, 0, &n) == 0 && n == 3 && memcmp(buf, "ack", 3) == 0);
  order[order_n++] = 'c';
  sppc_close(fd);
}

static void check_async(const int family) {
  uint16_t port = 0;
  server.lfd = listener(family, &port);
  server.n = 0;
  memset(server.got, 0, sizeof server.got);
  client.family = family;
  client.port = port;
  order_n = 0;

  int hs = -1, hc = -1;
  CHECK(sppc_async(&hs, (sppc_closure){server_task, NULL}) == 0);
  CHECK(sppc_async(&hc, (sppc_closure){client_task, NULL}) == 0);
  CHECK(sppc_await(hs) == 0);
  CHECK(sppc_await(hc) == 0);
  order[order_n] = '\0';
  CHECK(server.n == 11 && memcmp(server.got, "hello world", 11) == 0);

  // The server parks in accept, so the client starts before it finishes.
  CHECK(strcmp(order, "ACac") == 0 || strcmp(order, "ACca") == 0);
  sppc_close(server.lfd);
}

int main(void) {
  if (sppc_init() != 0) { return 1; }
  int probe = -1;
  have_v6 = sppc_socket(SPPC_AF_INET6, SPPC_SOCK_STREAM, 0, &probe) == 0;
  if (have_v6) {
    sppc_sockaddr_storage addr;
    sppc_set_sockaddr_v6(v6_loopback, 0, &addr);
    have_v6 = sppc_bind(probe, &addr) == 0; // the family can exist with no ::1 configured
    sppc_close(probe);
  }
  if (!have_v6) { printf("note: no IPv6 loopback here; the IPv6 checks are skipped\n"); }

  check_addresses();
  check_options();
  check_readiness();
  check_tcp(SPPC_AF_INET);
  check_udp(SPPC_AF_INET);
  // Only with a ring: without one, the server's accept blocks the thread
  // before the client task ever runs.
  if (_gt_io_ready()) { check_async(SPPC_AF_INET); }
  if (have_v6) {
    check_tcp(SPPC_AF_INET6);
    check_udp(SPPC_AF_INET6);
    if (_gt_io_ready()) { check_async(SPPC_AF_INET6); }
  }
  sppc_cleanup();
  return check_done();
}
