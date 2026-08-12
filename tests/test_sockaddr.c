// Regression test: wrappers that receive an address from the kernel must
// offer the full sockaddr_storage.
//
// The out-helper used to size the buffer from out_storage->ss_family, which
// the kernel has not written yet. Whenever that uninitialised read looked like
// AF_INET, the syscall was handed 16 bytes and quietly truncated the 28-byte
// v6 address, losing the tail of it.

#include <sppc/sppc.h>
#include "harness.h"

static int addr6_equal(struct sockaddr_storage *s, struct in6_addr const *want) {
  return memcmp(&((struct sockaddr_in6*)s)->sin6_addr, want, 16) == 0;
}

// Fill the caller's buffer with a pattern whose leading bytes read as AF_INET,
// which is the case the old helper got wrong.
static void poison_as_v4(struct sockaddr_storage *s) {
  memset(s, 0xAB, sizeof *s);
  ((struct sockaddr_in*)s)->sin_family = AF_INET;
}

int main(void) {
  TEST_BEGIN("sockaddr out-length");
  sppc_init();
  char detail[128];

  int fd = -1;
  CHECK_EQ("socket(AF_INET6)", sppc_socket(AF_INET6, SOCK_STREAM, 0, &fd), 0);

  struct sockaddr_storage bind_addr;
  memset(&bind_addr, 0, sizeof bind_addr);
  struct sockaddr_in6 *b6 = (struct sockaddr_in6*)&bind_addr;
  b6->sin6_family = AF_INET6;
  b6->sin6_port = htons(0);
  b6->sin6_addr = in6addr_loopback;                       // ::1
  CHECK_EQ("bind to [::1]:0", sppc_bind(fd, &bind_addr), 0);

  // --- getsockname ---
  struct sockaddr_storage got;
  poison_as_v4(&got);
  CHECK_EQ("getsockname", sppc_getsockname(fd, &got), 0);
  CHECK_EQ("  family is AF_INET6", got.ss_family, AF_INET6);
  {
    unsigned char *a = (unsigned char*)&((struct sockaddr_in6*)&got)->sin6_addr;
    snprintf(detail, sizeof detail,
             "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
             a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7],a[8],a[9],a[10],a[11],a[12],a[13],a[14],a[15]);
  }
  CHECK("  full ::1 returned, not truncated", addr6_equal(&got, &in6addr_loopback), detail);

  // --- getpeername on a connected pair ---
  CHECK_EQ("listen", sppc_listen(fd, 1), 0);
  struct sockaddr_storage server;
  poison_as_v4(&server);
  CHECK_EQ("getsockname (for port)", sppc_getsockname(fd, &server), 0);

  int client = -1;
  CHECK_EQ("socket(client)", sppc_socket(AF_INET6, SOCK_STREAM, 0, &client), 0);
  CHECK_EQ("connect", sppc_connect(client, &server), 0);

  // --- accept ---
  struct sockaddr_storage peer;
  poison_as_v4(&peer);
  int conn = -1;
  CHECK_EQ("accept", sppc_accept(fd, &peer, &conn), 0);
  CHECK_EQ("  accept family is AF_INET6", peer.ss_family, AF_INET6);
  CHECK("  accept returned full ::1", addr6_equal(&peer, &in6addr_loopback), "peer address");

  struct sockaddr_storage remote;
  poison_as_v4(&remote);
  CHECK_EQ("getpeername", sppc_getpeername(client, &remote), 0);
  CHECK_EQ("  family is AF_INET6", remote.ss_family, AF_INET6);
  CHECK("  full ::1 returned", addr6_equal(&remote, &in6addr_loopback), "peer address");

  close(conn); close(client); close(fd);

  // --- recvfrom over UDP, the other out-helper user ---
  int us = -1, ur = -1;
  CHECK_EQ("socket(udp rx)", sppc_socket(AF_INET6, SOCK_DGRAM, 0, &ur), 0);
  struct sockaddr_storage ua;
  memset(&ua, 0, sizeof ua);
  struct sockaddr_in6 *u6 = (struct sockaddr_in6*)&ua;
  u6->sin6_family = AF_INET6; u6->sin6_port = htons(0); u6->sin6_addr = in6addr_loopback;
  CHECK_EQ("bind(udp rx)", sppc_bind(ur, &ua), 0);

  struct sockaddr_storage bound;
  poison_as_v4(&bound);
  CHECK_EQ("getsockname(udp rx)", sppc_getsockname(ur, &bound), 0);

  CHECK_EQ("socket(udp tx)", sppc_socket(AF_INET6, SOCK_DGRAM, 0, &us), 0);
  ssize_t sent = 0;
  CHECK_EQ("sendto", sppc_sendto(us, "ping", 4, &bound, &sent), 0);
  CHECK_EQ("  sent 4 bytes", sent, 4);

  char buf[16] = {0};
  struct sockaddr_storage from;
  poison_as_v4(&from);
  ssize_t got_n = 0;
  CHECK_EQ("recvfrom", sppc_recvfrom(ur, buf, sizeof buf, &from, &got_n), 0);
  CHECK_EQ("  received 4 bytes", got_n, 4);
  CHECK("  payload intact", memcmp(buf, "ping", 4) == 0, buf);
  CHECK_EQ("  from family is AF_INET6", from.ss_family, AF_INET6);
  CHECK("  from address is full ::1", addr6_equal(&from, &in6addr_loopback), "sender address");

  close(us); close(ur);
  return TEST_SUMMARY();
}