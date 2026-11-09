---
name: x32-tested-in-debian-vm
description: "x32 is cross-built on the runner and tested in a Debian VM booted with syscall.x32=y; tools/x32_vm_test.sh does it, locally too"
updated: 2026-11-09
---

No x32 binary runs on a GitHub runner: Ubuntu's kernels leave the x32 syscalls out, and qemu-user has no x32 target
(`qemu-x86_64` rejects the binary: "Invalid ELF image for this architecture"). So `build-x32` in `_build.yaml`
cross-builds with `x86_64-linux-gnux32-gcc` and `tools/x32_vm_test.sh build` runs the tests in a Debian 13 VM under
KVM. cloud-init installs `libc6-x32 libx32gcc-s1 cmake`, adds `syscall.x32=y` to grub and reboots. The script then
copies the build tree to the same absolute path (ctest's test file holds absolute paths) and runs `ctest -LE host`.
`test_strict_c17` is labelled `host` because it runs the cross compiler, so it runs on the runner (`ctest -L host`).

**Why:** Use Debian's **generic** image, not genericcloud. The `-cloud` kernel has `# CONFIG_X86_X32_ABI is not set`,
so x32 binaries fail with "Exec format error" even with the boot flag. The standard amd64 kernel builds x32 in but
turns it off by default. Takes about a minute locally with KVM.
**How to apply:** For a local run, cross-build into `<dir>`, then run `tools/x32_vm_test.sh <dir> [image.qcow2]`
(passing an image skips the 440 MB download; its sha512 is checked either way). Update the pinned image and its
sha512 together. On WSL, `/dev/kvm` can come up owned by an unnamed gid rather than `kvm`, so qemu gets "Permission
denied" even for a `kvm` member: `sudo chgrp kvm /dev/kvm`. Also: WSL2's own kernel runs x32 natively.
