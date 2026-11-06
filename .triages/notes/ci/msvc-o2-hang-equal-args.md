---
name: msvc-o2-hang-equal-args
description: "msvc 14.52 (VS 18 Insiders) /O2 never finishes when a __forceinline function's checks see one GetSystemInfo-derived value as two arguments; sppc_mmap on Windows forwards out of line"
updated: 2026-11-04
---

cl 14.52.37025 (Visual Studio 18 Insiders) at `/O2` compiles forever, its memory climbing past 3 GB, on this:

```c
static size_t page_size(void) { SYSTEM_INFO i; GetSystemInfo(&i); return i.dwPageSize; }
static __forceinline void* f(size_t length, int64_t offset) {
  if (length == 0 || offset < 0) return NULL;
  return (void*)(uintptr_t)((uint64_t)offset + length);
}
int probe(void) { size_t n = page_size(); return f(n, (int64_t)n) != NULL; }
```

Constants, or two different variables, compile at once; so do `/Ob1` and `/d2SSAOptimizer-`. Found when
`test_mem.c` (`sppc_mmap(fd, len, ..., (int64_t)len)`) first went through msvc. The workaround: Windows'
`sppc_mmap` is a bare forwarder to `_sppc_win_mmap`, which is `_gnu_noinline` (emitted from sppc.c).

**Why:** every sppc api function is `__forceinline` under msvc, so its argument checks land in the caller; any of
them could meet this, in sppc's tests or in a user's program.

**How to apply:** if an msvc Release build hangs in `cl.exe` (ctest never starts; `tasklist` shows cl.exe growing),
bisect the test file down to the one call, then move that function's body out of line the same way. Check whether
a released msvc still has the bug before keeping such workarounds for good.
