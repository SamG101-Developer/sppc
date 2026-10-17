#pragma once

/// The i/o backend for a platform without one yet. Nothing is ever submitted:
/// every "_gt_try_*" declines, so each call takes its plain blocking syscall
/// and stops the thread's other tasks for the duration, and nothing is ever
/// outstanding for the scheduler to wait on. Correct everywhere, just not
/// concurrent - the starting point for a new platform's own backend.

#include <sppc/async/sched.h>
#include <sys/socket.h>
#include <sys/types.h>

_gnu_inline _gnu_cold
void _gt_io_setup(void) {}

_gnu_inline _gnu_cold
void _gt_io_teardown(void) {}

_gnu_inline
void _gt_io_poll(const int block) {
  (void)block;
}

_gnu_inline
void _gt_io_poll_until(const clockid_t clock, struct timespec const *deadline) {
  (void)clock;
  (void)deadline;
}

_gnu_inline
int _gt_io_ready(void) {
  return 0;
}

_gnu_inline
int _gt_try_read(const int fd, void *buf, const size_t n, ssize_t *res) {
  (void)fd; (void)buf; (void)n; (void)res;
  return 0;
}

_gnu_inline
int _gt_try_write(const int fd, void const *buf, const size_t n, ssize_t *res) {
  (void)fd; (void)buf; (void)n; (void)res;
  return 0;
}

_gnu_inline
int _gt_try_recv(const int fd, void *buf, const size_t n, const int flags, ssize_t *res) {
  (void)fd; (void)buf; (void)n; (void)flags; (void)res;
  return 0;
}

_gnu_inline
int _gt_try_send(const int fd, void const *buf, const size_t n, const int flags, ssize_t *res) {
  (void)fd; (void)buf; (void)n; (void)flags; (void)res;
  return 0;
}

_gnu_inline
int _gt_try_accept(const int fd, struct sockaddr *addr, socklen_t *addrlen, int *res) {
  (void)fd; (void)addr; (void)addrlen; (void)res;
  return 0;
}

_gnu_inline
int _gt_try_connect(const int fd, struct sockaddr const *addr, const socklen_t addrlen, int *res) {
  (void)fd; (void)addr; (void)addrlen; (void)res;
  return 0;
}

_gnu_inline
int _gt_try_fsync(const int fd, int *res) {
  (void)fd; (void)res;
  return 0;
}

_gnu_inline
int _gt_try_sleep(const clockid_t clock, const int flags, struct timespec const *duration, int *res) {
  (void)clock; (void)flags; (void)duration; (void)res;
  return 0;
}
