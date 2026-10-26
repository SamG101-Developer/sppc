---
name: x32-tested-locally-only
description: "x32 ships (ready) but CI only builds it: run its ctest natively on an x32-enabled kernel before merging changes"
updated: 2026-10-26
---

`x86_64-linux-gnux32` is `ready: true` with `test: false`: CI builds and ships it but cannot run it. GitHub's Ubuntu
kernels leave the x32 syscalls out, and qemu-user has no x32 target (`qemu-x86_64` rejects the binary: "Invalid ELF
image for this architecture"). A WSL2 kernel has `CONFIG_X86_X32_ABI=y`, so it runs natively there; on 2026-10-26 it
passed 34/34, the same skips as x86_64.

**Why:** the maintainer chose to ship it on local testing; it shares x86_64's code paths, which CI does test.
**How to apply:** after a change touching arch-specific code, syscalls, struct layouts or pointer-size assumptions,
build with `CC=x86_64-linux-gnux32-gcc cmake -S . -B <dir> -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=x86_64`
(Debian: `gcc-x86-64-linux-gnux32`, `libc6-x32`) and run `ctest --test-dir <dir>`. CI testing would need a
`qemu-system-x86_64` VM booting an x32-enabled kernel.
