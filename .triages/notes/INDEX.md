# Project notes

Paths are relative to this folder. Read a note before relying on its one-line summary.

- [Thread lock across a green yield](runtime/sppc-thread-lock-across-green-yield.md) — mutex over io_uring deadlocks
- [qemu ppc32 vdso CFA bug](ci/qemu-ppc32-vdso-sigframe-cfa.md) — rt sigtramp CFA 16 bytes low; fault backtraces stop at the vdso
- [Newest Debian qemu](ci/qemu-newest-debian-build.md) — CI pins the newest qemu-user .deb; test locally with that binary
