---
name: msvc-arm64-noreturn-tail-jump
description: "msvc arm64 /O2 compiles a function ending in a direct abort() to `b abort`, no frame, so a crash backtrace can't name it"
updated: 2026-11-09
---

On arm64, cl 14.52 at `/O2` compiles `static void f(void) { abort(); }` to a single `b abort`, a tail jump with no
frame of its own. The crash reporter's backtrace then has no `f+`. x64 cl keeps `call abort` with a frame. gcc and
clang don't tail-call noreturn functions. `test_process`'s `raises_abort` failed only on `aarch64-windows-msvc`
for this reason.

**Why:** a test that checks a backtrace for a function's name needs that function to have a frame. The runtime was
fine: the test function just had no frame.
**How to apply:** in a crash test, end the function as the others in `tests/test_process.c` do: call through a
`volatile` function pointer (`c_abort_fn`, `abort_fn`, `faults_fn`), then store to `after_call`. `_gnu_noinline`
alone doesn't stop a tail call. To check, compile with arm64 cl and `/FAs` (see [[windows-native-from-wsl]]) and
look at the function's assembly.
