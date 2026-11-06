---
name: platform-seven-files
description: "platform/ is seven files (shared, common, posix, linux, darwin, freebsd, windows); new code goes into one of them, and anything the same everywhere goes in shared.h or common.h"
updated: 2026-11-04
---

`headers/sppc/platform/` holds exactly: `shared.h` (helpers every platform shares that its api needs early: the
constant tables and their translation, socket / poll flags), `common.h` (the api every platform implements the same
way), `posix.h` (the
api Linux, Darwin and FreeBSD share), and one file per OS (`linux.h`, `darwin.h`, `freebsd.h`, `windows.h`). Each OS
file keeps one order: what the async runtime stands on (pthreads, clocks), then `async.h` / `backtrace.h`, then the
api (whose translation section includes `shared.h` first), then `common.h`. PORTING.md section 2 has the tree and the rules.

**Why:** the Windows port first grew a file per piece (`windows_base.h`, `pthread_win.h`, `fd_win.h`, `threads.h`,
`streams.h`, `memory.h`, `linux_freebsd.h`, `pthread_ext.h`), which Sam called "sticking bits on here and there"; they
were folded back on 2026-11-04 (machine code on Linux unchanged, every Windows test green).

**How to apply:** put new code in the section of the file it belongs to: Windows-only in `windows.h`, shared by the
POSIX platforms in `posix.h` (an `#if` for one that differs, `#if !SPPC_KERNEL_DARWIN` style when one platform
defines its own version), the same everywhere in `common.h` over names each platform provides, or in `shared.h`
when a platform's own api needs it before `common.h` is read (Sam chose this seventh file, 2026-11-04, for the
socket tables). Do not add a
platform file, and do not include `async.h` directly: everything enters through `sppc.h`.

Whenever a function is written for one OS (Windows, say), first check whether it, or the POSIX version already
there, is really the same everywhere, and if so move it into `common.h` instead of writing a second copy: the
string helpers were plain C library calls sitting in `posix.h` (Sam, 2026-11-04: "check if they can be in common
and moved out of posix"). What differs only by a C library name gets a small `_sppc_` name each platform defines
(`_sppc_memmem`, `_sppc_strcasecmp`), so the function itself stays common. Check machine code is unchanged on
Linux after such a move.
