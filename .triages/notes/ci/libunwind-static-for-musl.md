---
name: libunwind-static-for-musl
description: "The musl job builds libunwind itself (pinned, -fPIC, static, local-only) and links it into libsppc.so, which then needs only musl"
updated: 2026-10-26
---

musl's crash reporter needs libunwind (`cmake/Dependencies.cmake`). Alpine's `libunwind.a` is not position-independent
(`R_X86_64_PC32 ... recompile with -fPIC`) and its static build pulls in liblzma, so `build-musl` in `_build.yaml`
builds libunwind from its GitHub release (pinned URL + sha256; 1.8.3 on 2026-10-26) with `--with-pic --disable-shared`
and everything but local unwinding off, into `/opt/libunwind`, passed as `CMAKE_PREFIX_PATH`. A static archive found is
linked with `--exclude-libs,libunwind.a`, so no `unw_*` symbol is exported; the collect step fails if `libsppc.so` still
has a libunwind `NEEDED`. A shared libunwind (a local Alpine with `libunwind-dev`) still works as before.

**Why:** the shipped library must need only libc, like the glibc targets; checked on 2026-10-26 by linking an spp
hello world against it and running it in a bare Alpine 3.21 (plus `libgcc`, which spp's programs need, not sppc).
**How to apply:** bump `LIBUNWIND`/`LIBUNWIND_SHA256` to the newest release like qemu. Locally, reproduce in podman
with `alpine:3.21` and `--security-opt seccomp=unconfined` (io_uring). Don't add `libunwind-dev` back to the `apk add`:
its headers would mix with the built ones.
