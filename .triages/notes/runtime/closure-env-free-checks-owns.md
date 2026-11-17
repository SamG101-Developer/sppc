---
name: closure-env-free-checks-owns
description: "After running an s++ closure, free its environment only if the header's owns flag is set; a borrow-only closure's env is on a stack"
updated: 2026-11-16
---

An s++ closure's environment starts with `sppc_closure_env` (`owns`, `drop_captures`). Only an environment that
captured something it can carry away is heap allocated (`owns = 1`); a closure that captured only borrows keeps its
environment on the caller's stack. After the runtime has called a closure, release the environment with
`_sppc_closure_release_env` (`closure.h`), never `_sppc_env_free` directly. Until 2026-11-15 `sppc_pthread_once`,
`_sppc_thread_entry` and the green-thread scheduler (`async/sched.h`) freed it unconditionally, so `Once::call_once`
with a `|caps &x| { ... }` closure aborted in glibc's `free`.

**Why:** the compiler's `CClosure::from` passes whichever environment the closure has; only the header says whose it
is. `_sppc_closure_discard` (for a closure never called) already checked `owns`.

**How to apply:** any new entry point that takes an `sppc_closure` and runs it ends with
`_sppc_closure_release_env(closure.env)`; one that gives up without running it calls `_sppc_closure_discard`.
