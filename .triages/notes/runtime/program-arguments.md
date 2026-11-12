---
name: program-arguments
description: "sppc_set_args keeps argc/argv from spp's C main; Windows splits GetCommandLineW itself as UTF-8 (CRT argv is ANSI); no shell32"
updated: 2026-11-12
---

The compiler's generated C `main` calls `sppc_set_args(argc, argv)` before anything else. The S++ `main()` takes
nothing; a program reads its arguments with `std::process::args()`, which goes through `sppc_arg_count`,
`sppc_arg_len` and `sppc_arg`, with exact sizes, never a guessed buffer. POSIX keeps `argv` as given: the process
owns those strings for its whole life. Windows ignores it, because the C runtime's `argv` is in the ANSI code page
and garbles anything else (seen 2026-11-12: `é ü` came out as `? ?`). It splits `GetCommandLineW` itself
(`_sppc_win_split_args`, the C runtime's documented rules) and keeps UTF-8 copies in one block that lives as long as
the process.

**Why:** `CommandLineToArgvW` would do the split, but it is in `shell32`, which loads the shell's libraries into every
program for one call. `__wargv` is filled in only for `wmain` programs, and differs between msvcrt and UCRT.
**How to apply:** check a change to the splitter against Microsoft's table in `test_process.c` (Windows), and test
native runs with mingw and msvc (`ci/windows-native-from-wsl`). `sppc_getenv_len` gives the same exact size for
`sppc_getenv`; std's `get_env` used to guess 1024 bytes.
