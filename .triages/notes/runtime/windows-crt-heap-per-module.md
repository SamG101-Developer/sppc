---
name: windows-crt-heap-per-module
description: "On Windows, memory sppc frees (task environments) must come from sppc's own allocator: a different C runtime's malloc corrupts the heap"
updated: 2026-11-04
---

On Windows each module can carry its own C runtime, each with its own heap: mingw programs default to msvcrt.dll,
msvc ones to the ucrt. sppc frees memory its caller allocated, most directly each task's closure environment (freed
by the runtime when the task ends), so a buffer from one runtime's `malloc` reaches another's `free`. A mingw-built
loader driving an msvc-built `sppc.dll` died with 0xC0000374 (heap corruption) exactly so; built with msvc, the same
loader passed. On Linux there is one libc, so this never shows.

**Why:** the S++ side and `sppc.dll` are built separately, possibly by different toolchains.

**How to apply:** anything that crosses the dll boundary to be freed on the other side is allocated through sppc
(`sppc_malloc` and friends). On Windows that is also required by sppc's own heap: every block carries a 16-byte
header, so a plain `malloc` block given to `sppc_free` (or a task environment) is freed wrongly even within one CRT;
the runtime frees environments through `_sppc_env_free` (`sppc_free` there). When testing a Windows dll from a separate program, build that
program with the same toolchain (msvc for an msvc dll), or its own mallocs will look like sppc bugs. Exit codes read
through WSL interop keep only the low byte: 5 is 0xC0000005, 116 is 0xC0000374.
