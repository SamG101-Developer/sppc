---
name: debugger-stop-on-failure
description: "sppc_debug_fatal: exported no-op called before _exit on every failure by call; spp's tools break on it (like rust_panic); never rename"
updated: 2026-11-11
---

A failure that ends the process by call (a failed check, `sppc_abort`) ends in `_sppc_bt_fail_end`, which writes the
report and calls `_exit`. A debugger sees only an exit, so `_sppc_bt_fail_end` first calls `sppc_debug_fatal`: an
exported, never-inlined function whose body only bumps a volatile counter (`_sppc_bt_fatal_stops`). spp-idea and any
`spp debug` command set a breakpoint on it, as Rust's tooling does on `rust_panic`, and the failing code's frames are
still on the stack. Fatal signals need nothing: a debugger stops on them before the handler runs.

**Why:** one stable name covers every way the process fails by call. Detecting a debugger and raising SIGTRAP was
rejected: it needs code for each platform, and under strace it changes the exit status.
**How to apply:** never rename it, or let it be inlined or folded: the counter keeps its body unique (msvc's /OPT:ICF
merges identical bodies). A breakpoint on it has to be pending, as libsppc loads after the program starts.
