---
name: windows-native-from-wsl
description: "Test the Windows port from WSL: mingw-w64 gcc/clang builds run natively through interop; msvc via a vcvars64 .bat run with cmd.exe"
updated: 2026-11-08
---

On a WSL2 dev box the Windows port needs no VM or CI round trip. A mingw build runs natively, since WSL interop
executes `.exe` files: `x86_64-w64-mingw32-gcc -std=c17 -pedantic-errors -Iheaders -o t.exe tests/test_pthread_win.c
sources/sppc/sppc.c -lsynchronization && ./t.exe` (clang works too: `--target=x86_64-w64-mingw32 -fuse-ld=lld`).
For msvc, write a `.bat` that `call`s `C:\Program Files\Microsoft Visual Studio\<ver>\<edition>\VC\Auxiliary\Build\
vcvars64.bat`, then runs `cl` or `cmake.exe -G Ninja` on the `\\wsl.localhost\<distro>\...` source path with the
build directory under `%TEMP%`. Run it with `cmd.exe /c "$(wslpath -w x.bat)"`; cmd's "UNC paths are not supported"
warning is harmless.

For arm64, `call vcvarsamd64_arm64.bat` instead and pass `-DCMAKE_SYSTEM_NAME=Windows -DCMAKE_SYSTEM_PROCESSOR=ARM64`
(the Hostx64\arm64 tools must be installed): that builds the library and every test, but an x64 host cannot run
them, so only CI's `windows-11-arm` runner tests `aarch64-windows-msvc`.

**Why:** local runs catch a Windows break in seconds rather than a CI round trip; mingw and msvc differ (`_Thread_local` is emulated TLS under mingw gcc, `__forceinline`
under msvc `/Od`), so run both.

**How to apply:** after any change under `platform/windows.h`, `platform/common.h`, `async/` or the Windows
branches, run every Windows test (`tests/test_*_win.c` plus `SPPC_WINDOWS_PORTED` in CMakeLists.txt; ctest through
`cmake.exe` runs them all) with mingw gcc and with msvc in both Debug and Release. Build `sppc.c` with `-Werror`, but
not the tests: the shared ones trip gcc's `-Warray-bounds` on closure environments smaller than `sppc_closure_env`. A
hand-run `cl` needs `/utf-8` as CMake gives it: without it msvc reads source in the system code page, so a UTF-8
literal in a test passes under mingw and fails under msvc. Keep test literals ASCII (`\u` escapes) besides.
Symbolic links need Developer Mode (Settings, System, For developers) or an elevated shell, and the interop shell is
neither: `test_fs.c` then prints that it skipped its link checks, so the link code has not run until one is on.

A test that times out only under ctest but runs from WSL is usually a dialog nobody answers: msvc's debug C
library reports `abort()` in a window (and run without a window station, as interop runs it, breaks into a debugger
instead). `check.h`'s `check_child` sends those reports to stderr; a new crash-checking test needs it called from
main. A WSL `timeout` kills `cmd.exe` but not the `cl.exe` / test `.exe` it started: `taskkill /F /IM <name>` after.
