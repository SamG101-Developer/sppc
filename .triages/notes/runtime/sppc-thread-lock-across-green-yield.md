---
name: sppc-thread-lock-across-green-yield
description: "a pthread mutex held across an io_uring-routed call deadlocks green tasks on one thread; sppc stdio now locks via _gt_mutex_lock"
updated: 2026-09-18
---

Green tasks all share one OS thread (the runtime `_gt_R` is `_Thread_local`). `sppc_write`/`sppc_read` route
through io_uring inside a task and **park it**, so any pthread mutex held around them is held by a parked task. A
second task hitting `pthread_mutex_lock` then blocks the only thread that could resume the holder. Symptom
(2026-09-18): two `async` tasks that `println`: the first prints its first line *without the newline*, then everything
hangs. gdb shows `futex_wait` on `_stdout_mutex` under `_gt_trampoline`.

Fix in sppc: `_gt_mutex_lock` in `async2.h` (trylock; on EBUSY run another ready task or wait on the ring; block
the thread only when nothing on it is runnable or in flight). `_return_if_guarded_err` in `macros.h` uses it, which
covers stdin/stdout/stderr.

std `Mutex` fixed the same way (2026-09-18): `_gt_mutex_lock_until` / `_gt_cond_wait_until` in `async2.h` back
`sppc_pthread_mutex_{lock,clocklock}` and `sppc_pthread_cond_{wait,clockwait}`. A cond wait with other work on the
thread unlocks, steps, relocks and returns as a *spurious wake-up*; only with nothing else runnable does it do the real
wait. Caveat: two tasks on one thread waiting on a condvar signalled from another OS thread ping-pong (CPU spin, not
a hang).

`RwLock` too, same day: every lock kind goes through one `_gt_lock_until(try, lock, timed, clock, deadline)` macro
in `async2.h`. On the old runtime a reader task under a writer task got `EDEADLK` (glibc sees the same OS thread),
not a hang.

Found on the way, fixed for both: std passes a *relative* timeout, pthread timed calls take an *absolute* deadline (so
a contended timed lock timed out at once); and sppc mapped ETIMEDOUT/EBUSY to `1` while std checks
`cffi::etimedout`/`cffi::ebusy`. `SpinLock` done too (`_gt_spin_lock`); its std `try_lock` had no busy case, so it
now returns `WouldBlock or ThreadErr` (std change in `spin_lock.spp`). The barrier's serial-thread `1` is deliberate.

**Fairness (same day):** the lock loop alone let the releasing task barge straight back in (waiters only sit on the
run queue), so two `async` printers ran 0-199 then 0-199. Now waiters queue on a thread-local FIFO
(`gt_lock_waiter`, on the waiter's stack) and every unlock calls `_gt_lock_released`, which switches to the oldest
ready waiter ahead of the releaser. No ownership transfer, so it works for all lock kinds. Also `println`/`eprintln`
in std are one write (`Str::from(str) + "\n"`) - two writes let another task's line land before the newline.
A task that never does I/O or takes a contended lock still runs to completion - cooperative by design.

Probe outside `project/`: a scratch project dir with `spp.toml`, `src/`, `vcs` symlinked - but `ffi` must be a real
copy, a symlinked `ffi` is silently skipped and the link has no `-lsppc`.

**How to apply:** when a green-task program hangs, look for a thread-level lock held across a routed syscall before
blaming the scheduler. Related: `sppc-sibling-repo` (spp notes), `spp-green-task-detach-uaf` (spp notes).
