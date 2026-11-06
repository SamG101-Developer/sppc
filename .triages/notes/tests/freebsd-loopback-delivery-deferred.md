---
name: freebsd-loopback-delivery-deferred
description: "FreeBSD's loopback queues packets to netisr: data written to a TCP socket is not yet readable at the peer when write returns"
updated: 2026-11-06
---

On FreeBSD, `lo0` passes packets to `netisr_queue` (in `if_simloop`), so another thread delivers them. Data written to
one end of a loopback TCP connection may not be readable at the peer yet when `write` returns. On Linux it already is.
A `poll` over several descriptors returns when the first one is ready, so `test_net`'s readiness check saw `n == 1`
(the client writable, the server's data not there yet) on the FreeBSD runner.

**Why:** a socket test that writes and then checks the peer with a zero timeout, or with a multi-descriptor poll, passes
on Linux and is flaky or fails on FreeBSD.
**How to apply:** in tests, poll the receiving socket alone with a timeout before checking it together with others or
reading it without blocking.
