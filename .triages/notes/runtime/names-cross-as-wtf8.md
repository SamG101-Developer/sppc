---
name: names-cross-as-wtf8
description: "On Windows, paths, environment names/values and arguments cross the API as WTF-8 (_sppc_win_wtf8 / _sppc_win_wide), not UTF-8; the console stays UTF-8"
updated: 2026-11-16
---

Windows' names are UTF-16 that need not pair their surrogates. Every name, value or argument sppc hands across its
`char*` API on Windows is WTF-8: UTF-8 that also carries a lone surrogate as the three bytes its code point would take
(`ED A0..BF xx`). `_sppc_win_wtf8` converts from UTF-16 and `_sppc_win_wide` converts back. `_sppc_win_wpath` (paths,
environment names and values) and `_sppc_win_wtf8_path` (getcwd, realpath) use them, and so do readlink, the link size
in stat, getenv and the argument splitter. `_sppc_win_wide` refuses bytes that are not WTF-8 (EILSEQ). A lead and a
trail surrogate written apart come out as the pair they make. The console is the one exception: it stays UTF-8, with
U+FFFD for what is not, since it is text.

**Why:** with `WideCharToMultiByte`, a lone surrogate became U+FFFD, so a file listed under such a name could not be
opened again. With `MB_ERR_INVALID_CHARS` the other way, such a name could not be passed at all. SPP-STL's
`std::os::os_string::OsStr` holds these bytes as they are (2026-11-16); POSIX passes bytes through unchanged.

**How to apply:** use the two helpers for any new crossing of a name, value or argument, never the Win32 converters.
Test with a name made by `CreateFileW` (as `check_lone_surrogate` in `test_files_win.c` does). Use only `\x`/`\u`
escapes in test literals.
