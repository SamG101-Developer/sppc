---
name: io-uring-ignores-o-nonblock
description: "io_uring read/write/recv/send/accept wait for readiness even on an O_NONBLOCK fd; _gt_try_* decline such fds so POSIX's EAGAIN comes from the plain syscall"
updated: 2026-11-04
---

Submitted through the ring, `IORING_OP_READ`, `_WRITE`, `_RECV`, `_SEND` and `_ACCEPT` on a socket made O_NONBLOCK
with `fcntl` do not complete with -EAGAIN: the kernel arms a poll and the task parks until data comes, i.e. forever
in a test that expects EAGAIN (seen on 6.18 WSL2). So `_gt_io_begin_fd` in `async/io_uring.h` reads the fd's flags
(`fcntl(F_GETFL)`, one extra syscall per ring op) and declines when O_NONBLOCK is set, and `_gt_try_recv` / `_send`
decline MSG_DONTWAIT; the plain syscall then returns at once.

**Why:** found 2026-11-04 when `test_net.c` gained an O_NONBLOCK recv check during the Windows socket port: the
Linux run hung while the blocking backend gave EAGAIN. Pre-existing since the ring backend was written.

**How to apply:** any new fd-based `_gt_try_*` uses `_gt_io_begin_fd`, not `_gt_io_begin`. If the fcntl cost
matters, the alternative is tracking O_NONBLOCK in sppc (it sets it through `sppc_fcntl_set` / `SOCK_NONBLOCK`),
but fds from elsewhere (inherited, a host's) would be missed. A kqueue backend has the opposite problem: it needs
fds non-blocking itself, so it must remember which ones the program made so.
