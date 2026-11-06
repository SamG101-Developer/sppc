#include "check.h"

// Windows only: the i/o completion port backend (async/iocp.h). A task
// waiting on a socket or a sleep parks, and the thread's other tasks run
// meanwhile; the operation's result is what the plain call would have given.

static char order[64];
static int norder = 0;
static void note(const char c) {
  if (norder < (int)sizeof order - 1) { order[norder++] = c; }
  order[norder] = '\0';
}
static void reset(void) {
  norder = 0;
  order[0] = '\0';
}

/// A listening TCP socket on IPv4 loopback, and its port.
static int listener(uint16_t *port) {
  int fd = -1;
  sppc_sockaddr_storage addr, bound;
  const uint8_t lo[4] = {127, 0, 0, 1};
  uint8_t ip[4];
  CHECK(sppc_socket(SPPC_AF_INET, SPPC_SOCK_STREAM, 0, &fd) == 0);
  sppc_set_sockaddr_v4(lo, 0, &addr);
  CHECK(sppc_bind(fd, &addr) == 0 && sppc_listen(fd, 8) == 0);
  CHECK(sppc_getsockname(fd, &bound) == 0);
  sppc_get_sockaddr_v4(&bound, ip, port);
  return fd;
}

static void loopback(const uint16_t port, sppc_sockaddr_storage *out) {
  const uint8_t lo[4] = {127, 0, 0, 1};
  sppc_set_sockaddr_v4(lo, port, out);
}

/// A connected pair, through the plain calls (no task involved).
static void pair(int *a, int *b) {
  uint16_t port = 0;
  const int lfd = listener(&port);
  sppc_sockaddr_storage addr, peer;
  loopback(port, &addr);
  CHECK(sppc_socket(SPPC_AF_INET, SPPC_SOCK_STREAM, 0, a) == 0);
  CHECK(sppc_connect(*a, &addr) == 0);
  CHECK(sppc_accept(lfd, &peer, b) == 0);
  sppc_close(lfd);
}

// ==================== RECEIVING ====================

static struct { int fd; char buf[16]; ssize_t got; int err; bool use_read; } rx;
static struct { int fd; bool use_write; } tx;

static void receiver(void *e) {
  (void)e;
  note('R');
  rx.err = rx.use_read ? sppc_read(rx.buf, 1, sizeof rx.buf, rx.fd, &rx.got)
    : sppc_recv(rx.fd, rx.buf, sizeof rx.buf, 0, &rx.got);
  note('r');
}

static void sender(void *e) {
  (void)e;
  note('W'); // runs while the receiver is parked on the port
  ssize_t sent = 0;
  const int err = tx.use_write ? sppc_write("hello", 1, 5, tx.fd, &sent) : sppc_send(tx.fd, "hello", 5, 0, &sent);
  CHECK(err == 0 && sent == 5);
  note('w');
}

static void check_receive_parks(const bool through_read_write) {
  int a = -1, b = -1;
  pair(&a, &b);
  reset();
  rx.fd = b;
  rx.got = -1;
  rx.use_read = through_read_write;
  tx.fd = a;
  tx.use_write = through_read_write;
  int hr = -1, hw = -1;
  CHECK(sppc_async(&hr, (sppc_closure){receiver, NULL}) == 0);
  CHECK(sppc_async(&hw, (sppc_closure){sender, NULL}) == 0);
  CHECK(sppc_await(hr) == 0 && sppc_await(hw) == 0);
  // The receiver parked and the sender ran before the receive finished.
  // The send waits on the port too, so which of the two then wakes first is
  // the port's to say: either order is right.
  CHECK(strcmp(order, "RWwr") == 0 || strcmp(order, "RWrw") == 0);
  CHECK(rx.err == 0 && rx.got == 5 && memcmp(rx.buf, "hello", 5) == 0);

  // The peer gone: the end of the stream, through the port too.
  reset();
  sppc_close(a);
  CHECK(sppc_async(&hr, (sppc_closure){receiver, NULL}) == 0);
  CHECK(sppc_await(hr) == 0);
  CHECK(rx.err == 0 && rx.got == 0);
  sppc_close(b);
}

// ==================== ACCEPT AND CONNECT ====================

static struct { int lfd; int fd; int err; sppc_sockaddr_storage peer; } acc;
static struct { uint16_t port; int fd; int err; } con;

static void acceptor(void *e) {
  (void)e;
  note('A');
  acc.err = sppc_accept(acc.lfd, &acc.peer, &acc.fd);
  note('a');
}

static void connector(void *e) {
  (void)e;
  note('C');
  sppc_sockaddr_storage addr;
  loopback(con.port, &addr);
  CHECK(sppc_socket(SPPC_AF_INET, SPPC_SOCK_STREAM, 0, &con.fd) == 0);
  con.err = sppc_connect(con.fd, &addr); // unbound: ConnectEx needs it bound, which sppc does
  note('c');
}

static void check_accept_and_connect(void) {
  reset();
  acc.lfd = listener(&con.port);
  acc.fd = con.fd = -1;
  int ha = -1, hc = -1;
  CHECK(sppc_async(&ha, (sppc_closure){acceptor, NULL}) == 0);
  CHECK(sppc_async(&hc, (sppc_closure){connector, NULL}) == 0);
  CHECK(sppc_await(ha) == 0 && sppc_await(hc) == 0);
  CHECK(order[0] == 'A' && order[1] == 'C'); // the acceptor parked, and the connector ran
  CHECK(acc.err == 0 && acc.fd >= 0 && con.err == 0);

  // Both ends as a plain accept and connect leave them: names, the peer's
  // address, shutdown, data both ways.
  sppc_sockaddr_storage mine, theirs;
  uint8_t ip[4];
  uint16_t my_port = 0, peer_port = 0, their_port = 0;
  CHECK(sppc_getsockname(con.fd, &mine) == 0);
  sppc_get_sockaddr_v4(&mine, ip, &my_port);
  CHECK(my_port != 0);
  sppc_get_sockaddr_v4(&acc.peer, ip, &peer_port);
  CHECK(peer_port == my_port && ip[0] == 127);
  CHECK(sppc_getpeername(acc.fd, &theirs) == 0);
  sppc_get_sockaddr_v4(&theirs, ip, &their_port);
  CHECK(their_port == my_port);
  CHECK(sppc_getpeername(con.fd, &theirs) == 0);
  sppc_get_sockaddr_v4(&theirs, ip, &their_port);
  CHECK(their_port == con.port);
  ssize_t n = 0;
  char buf[8];
  CHECK(sppc_send(con.fd, "ping", 4, 0, &n) == 0 && n == 4);
  CHECK(sppc_recv(acc.fd, buf, sizeof buf, 0, &n) == 0 && n == 4 && memcmp(buf, "ping", 4) == 0);
  CHECK(sppc_shutdown(con.fd, SPPC_SHUT_WR) == 0);
  CHECK(sppc_recv(acc.fd, buf, sizeof buf, 0, &n) == 0 && n == 0);
  sppc_close(acc.fd);
  sppc_close(con.fd);

  // Refused: the error a plain connect gives, from a task.
  reset();
  const int lfd2 = listener(&con.port);
  sppc_close(lfd2); // nobody listens there now
  CHECK(sppc_async(&hc, (sppc_closure){connector, NULL}) == 0);
  CHECK(sppc_await(hc) == 0);
  CHECK(con.err == SPPC_ECONNREFUSED);
  sppc_close(con.fd);
  sppc_close(acc.lfd);
}

// ==================== DATAGRAMS ====================

static struct { int fd; char buf[4]; ssize_t got; int err; sppc_sockaddr_storage from; } dgram;

static void datagram_receiver(void *e) {
  (void)e;
  note('R');
  dgram.err = sppc_recvfrom(dgram.fd, dgram.buf, sizeof dgram.buf, &dgram.from, &dgram.got);
  note('r');
}

static void check_datagrams(void) {
  int a = -1, b = -1;
  const uint8_t lo[4] = {127, 0, 0, 1};
  sppc_sockaddr_storage addr, bound;
  CHECK(sppc_socket(SPPC_AF_INET, SPPC_SOCK_DGRAM, 0, &a) == 0 && sppc_socket(SPPC_AF_INET, SPPC_SOCK_DGRAM, 0, &b) == 0);
  sppc_set_sockaddr_v4(lo, 0, &addr);
  CHECK(sppc_bind(b, &addr) == 0 && sppc_bind(a, &addr) == 0 && sppc_getsockname(b, &bound) == 0);
  reset();
  dgram.fd = b;
  int h = -1;
  CHECK(sppc_async(&h, (sppc_closure){datagram_receiver, NULL}) == 0);
  sppc_async_yield(); // the receiver parks
  note('W');
  ssize_t sent = 0;
  CHECK(sppc_sendto(a, "too long", 8, &bound, &sent) == 0 && sent == 8);
  CHECK(sppc_await(h) == 0);
  CHECK(strcmp(order, "RWr") == 0);
  // Cut to the buffer, as POSIX's: Windows' error for it is not one here.
  CHECK(dgram.err == 0 && dgram.got == 4 && memcmp(dgram.buf, "too ", 4) == 0);
  sppc_sockaddr_storage a_name;
  uint8_t ip[4];
  uint16_t from_port = 0, a_port = 0;
  CHECK(sppc_getsockname(a, &a_name) == 0);
  sppc_get_sockaddr_v4(&a_name, ip, &a_port);
  sppc_get_sockaddr_v4(&dgram.from, ip, &from_port);
  CHECK(from_port == a_port);
  sppc_close(a);
  sppc_close(b);
}

// ==================== SLEEPING ====================

static void sleeps(void *e) {
  (void)e;
  const sppc_timespec d = {0, 40 * 1000 * 1000};
  CHECK(sppc_clock_nanosleep(SPPC_CLOCK_MONOTONIC, 0, &d) == 0);
  note('s');
}

static void sleeps_until(void *e) {
  (void)e;
  sppc_timespec t;
  CHECK(sppc_clock_gettime(SPPC_CLOCK_REALTIME, &t) == 0);
  t.nsec += 40 * 1000 * 1000;
  if (t.nsec >= 1000 * 1000 * 1000) {
    t.sec += 1;
    t.nsec -= 1000 * 1000 * 1000;
  }
  CHECK(sppc_clock_nanosleep(SPPC_CLOCK_REALTIME, SPPC_TIMER_ABSTIME, &t) == 0);
  sppc_timespec now;
  CHECK(sppc_clock_gettime(SPPC_CLOCK_REALTIME, &now) == 0);
  CHECK(now.sec > t.sec || (now.sec == t.sec && now.nsec >= t.nsec)); // never early
  note('s');
}

static void spins(void *e) {
  (void)e;
  for (int i = 0; i < 3; ++i) {
    note('.');
    sppc_async_yield();
  }
}

static int64_t mono_ns(void) {
  sppc_timespec t;
  sppc_clock_gettime(SPPC_CLOCK_MONOTONIC, &t);
  return (int64_t)t.sec * 1000000000LL + t.nsec;
}

static int64_t short_sleep_took;
static void sleeps_2ms(void *e) {
  (void)e;
  const sppc_timespec d = {0, 2 * 1000 * 1000};
  const int64_t start = mono_ns();
  CHECK(sppc_clock_nanosleep(SPPC_CLOCK_MONOTONIC, 0, &d) == 0);
  short_sleep_took = mono_ns() - start;
}

static int woke[3], nwoke = 0;
static void sleeps_for(void *e) {
  const int ms = *(int*)e; // the environment is the runtime's, freed when the task ends
  const sppc_timespec d = {0, ms * 1000 * 1000};
  CHECK(sppc_clock_nanosleep(SPPC_CLOCK_MONOTONIC, 0, &d) == 0);
  woke[nwoke++] = ms;
}

static void check_sleeps(void) {
  // A sleeping task leaves the thread to the others.
  void (*const sleepers[])(void*) = {sleeps, sleeps_until};
  for (size_t i = 0; i < 2; ++i) {
    reset();
    int hs = -1, hp = -1;
    CHECK(sppc_async(&hs, (sppc_closure){sleepers[i], NULL}) == 0);
    CHECK(sppc_async(&hp, (sppc_closure){spins, NULL}) == 0);
    CHECK(sppc_await(hs) == 0 && sppc_await(hp) == 0);
    CHECK(strcmp(order, "...s") == 0);
  }

  // Several, woken in deadline order whatever the order they slept in.
  int h[3];
  const int ms[3] = {30, 10, 20};
  for (int i = 0; i < 3; ++i) {
    int *const env = (int*)sppc_malloc(sizeof *env);
    *env = ms[i];
    CHECK(sppc_async(&h[i], (sppc_closure){sleeps_for, env}) == 0);
  }
  for (int i = 0; i < 3; ++i) { CHECK(sppc_await(h[i]) == 0); }
  CHECK(nwoke == 3 && woke[0] == 10 && woke[1] == 20 && woke[2] == 30);

  // Not rounded up to the scheduler's 15.6 ms tick, as a port's own
  // timeout would be (the sleep timer is a high-resolution one). The best
  // of a few, as a loaded machine can be late any one time.
  int64_t best = INT64_MAX;
  for (int i = 0; i < 5; ++i) {
    int hs = -1;
    CHECK(sppc_async(&hs, (sppc_closure){sleeps_2ms, NULL}) == 0);
    CHECK(sppc_await(hs) == 0);
    if (short_sleep_took < best) { best = short_sleep_took; }
  }
  CHECK(best >= 2 * 1000 * 1000 && best < 10 * 1000 * 1000);
}

// ==================== THREADS ====================

static int shared_a = -1, shared_b = -1;

static void other_thread_uses_it(void *e) {
  (void)e;
  // Tied to the main thread's port: here, the plain calls.
  ssize_t n = 0;
  char buf[8];
  CHECK(sppc_send(shared_b, "back", 4, 0, &n) == 0 && n == 4);
  CHECK(sppc_recv(shared_b, buf, sizeof buf, 0, &n) == 0 && n == 2 && memcmp(buf, "ok", 2) == 0);
}

static void check_other_thread(void) {
  pair(&shared_a, &shared_b);
  // A task on this thread ties shared_b to this thread's port.
  reset();
  rx.fd = shared_b;
  rx.use_read = false;
  tx.fd = shared_a;
  tx.use_write = false;
  int hr = -1, hw = -1;
  CHECK(sppc_async(&hr, (sppc_closure){receiver, NULL}) == 0);
  CHECK(sppc_async(&hw, (sppc_closure){sender, NULL}) == 0);
  CHECK(sppc_await(hr) == 0 && sppc_await(hw) == 0 && rx.got == 5);

  uint64_t t = 0;
  CHECK(sppc_pthread_create((sppc_closure){other_thread_uses_it, NULL}, &t) == 0);
  ssize_t n = 0;
  char buf[8];
  CHECK(sppc_recv(shared_a, buf, sizeof buf, 0, &n) == 0 && n == 4 && memcmp(buf, "back", 4) == 0);
  CHECK(sppc_send(shared_a, "ok", 2, 0, &n) == 0);
  CHECK(sppc_pthread_join(&t) == 0);
  sppc_close(shared_a);
  sppc_close(shared_b);
}

// A thread Windows made, not sppc, ends with a task still waiting on a
// receive: the runtime's teardown cancels it and waits it out, so the
// kernel never writes into the task once it is freed.
static int abandoned_fd = -1;
static void waits_forever(void *e) {
  (void)e;
  char buf[4];
  ssize_t n = 0;
  sppc_recv(abandoned_fd, buf, sizeof buf, 0, &n);
}

static DWORD WINAPI leaves_a_receive_behind(LPVOID e) {
  (void)e;
  int h = -1;
  sppc_async(&h, (sppc_closure){waits_forever, NULL});
  sppc_async_yield(); // it starts, and parks on the port
  return 0; // never awaited: the thread's end tears the runtime down
}

static void check_teardown_cancels(void) {
  int a = -1;
  pair(&a, &abandoned_fd);
  HANDLE const t = CreateThread(NULL, 0, leaves_a_receive_behind, NULL, 0, NULL);
  CHECK(t != NULL);
  if (t != NULL) {
    CHECK(WaitForSingleObject(t, 10000) == WAIT_OBJECT_0);
    CloseHandle(t);
  }
  // The socket still works, from here, with the plain calls.
  ssize_t n = 0;
  char buf[4];
  CHECK(sppc_send(a, "x", 1, 0, &n) == 0);
  CHECK(sppc_recv(abandoned_fd, buf, sizeof buf, 0, &n) == 0 && n == 1);
  sppc_close(a);
  sppc_close(abandoned_fd);
}

int main(void) {
  if (sppc_init() != 0) { return 1; }
  check_child();
  if (!_gt_io_ready()) {
    printf("SKIPPED (no completion port)\n");
    return CHECK_SKIP_STATUS;
  }
  check_receive_parks(false);
  check_receive_parks(true);
  check_accept_and_connect();
  check_datagrams();
  check_sleeps();
  check_other_thread();
  check_teardown_cancels();
  sppc_cleanup();
  return check_done();
}
