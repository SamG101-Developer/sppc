# Project notes

Paths are relative to this folder. Read a note before relying on its one-line summary.

- [Thread lock across a green yield](runtime/sppc-thread-lock-across-green-yield.md) — mutex over io_uring deadlocks
- [qemu ppc32 vdso CFA bug](ci/qemu-ppc32-vdso-sigframe-cfa.md) — rt sigtramp CFA 16 bytes low; fault backtraces stop at the vdso
- [qemu from our fork](ci/qemu-from-spp-fork.md) — CI pins a static release of SamG101-Developer/qemu (U17-U19, 32-bit mprotect/msync ENOMEM), same as spp
- [Arm toolchain for aarch64_be](ci/arm-gnu-toolchain-aarch64-be.md) — Ubuntu has none; pinned gitlab.arm.com tarball, qemu -L its sysroot
- [x32 tested in a Debian VM](ci/x32-tested-in-debian-vm.md) — cross-built, ctest in a KVM Debian VM booted syscall.x32=y (generic image: -cloud kernel has no x32); tools/x32_vm_test.sh
- [GHA null == false](ci/gha-null-equals-false.md) — unset matrix key is null, equal to false; `!= false` skipped ctest on every target from #15 to 2026-11-09
- [Static libunwind for musl](ci/libunwind-static-for-musl.md) — CI builds pinned PIC libunwind, linked into libsppc.so; needs only musl
- [FreeBSD VM for local testing](ci/freebsd-vm-local-testing.md) — cloud-init VM image under KVM, ssh in, build natively; ktrace for hangs
- [Windows tests from WSL](ci/windows-native-from-wsl.md) — mingw .exe runs natively via interop; msvc through a vcvars64 .bat under cmd.exe; arm64 cross-builds only
- [Windows arm64 runner](ci/windows-arm64-runner-actions.md) — msvc-dev-cmd needs arch: arm64 or builds x64; harden-runner has no agent there and skips
- [Windows: one heap per C runtime](runtime/windows-crt-heap-per-module.md) — memory sppc frees must come from sppc's allocator; another CRT's malloc corrupts the heap
- [platform/ is seven files](layout/platform-seven-files.md) — shared, common, posix, linux, darwin, freebsd, windows; anything the same everywhere goes in shared.h/common.h, checked whenever a port adds a function
- [io_uring ignores O_NONBLOCK](runtime/io-uring-ignores-o-nonblock.md) — ring ops wait on non-blocking fds; `_gt_try_*` decline them so EAGAIN comes from the syscall
- [msvc /O2 hang on equal args](ci/msvc-o2-hang-equal-args.md) — cl 14.52 never finishes inlining checks on one value passed twice; sppc_mmap forwards out of line
- [Bindgen libclang/clang mismatch](tools/bindgen-libclang-resource-dir-mismatch.md) — builtin headers from another clang version hide stdint types; tools/main.py now picks a matching clang
- [Byte pointer params](tools/bindgen-byte-pointer-params.md) — void*/char*/unsigned char* params → CRefBytePtr/CMutBytePtr by constness; returns, fields, handles stay CVoidPtr
- [FreeBSD loopback is deferred](tests/freebsd-loopback-delivery-deferred.md) — written TCP data reaches the peer later; poll the receiver alone before multi-fd or zero-timeout checks
- [msvc arm64 noreturn tail jump](tests/msvc-arm64-noreturn-tail-jump.md) — direct abort() becomes `b abort`, no frame; crash tests call through a volatile pointer then store after_call
- [Branch protection](runtime/branch-protection.md) — AArch64 ELF built -mbranch-protection=bti+pac-ret; switch_aapcs64.S needs its own bti c + property note; BTI/PAC faults reported as CFI failures, no stack walk
- [Source lines from a table](runtime/source-lines-table.md) — spp links a line table in, main hands it over (sppc_set_source_lines); format in backtrace/common.h; no DWARF parsing here
- [Debugger stop on failure](runtime/debugger-stop-on-failure.md) — sppc_debug_fatal, called before _exit on check failures/abort; tools break on it (like rust_panic); never rename
