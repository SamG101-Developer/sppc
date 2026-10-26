---
name: freebsd-vm-local-testing
description: "Test sppc on FreeBSD locally: the BASIC-CLOUDINIT VM image under qemu-system with KVM, a cloud-init seed for an ssh key, build natively"
updated: 2026-10-26
---

sppc's FreeBSD port is built and tested natively in a FreeBSD VM, not cross-built. Locally: the newest release's
`FreeBSD-<v>-RELEASE-amd64-BASIC-CLOUDINIT-ufs.qcow2.xz` from `download.freebsd.org/releases/VM-IMAGES/<v>-RELEASE/
amd64/Latest/` (sha256 in its `CHECKSUM.SHA256`), `qemu-img resize` it, and a NoCloud seed ISO (`xorriso -as mkisofs
-V cidata` over `meta-data` + `user-data`: a user with an ssh key, `sudo: ALL=(ALL) NOPASSWD:ALL`, `packages: [sudo,
cmake, git]`). Boot with `qemu-system-x86_64 -enable-kvm -cpu host -drive file=disk.qcow2,if=virtio -drive
file=seed.iso,media=cdrom -nic user,hostfwd=tcp:127.0.0.1:<port>-:22`; ssh is up in about a minute. Copy the tree in
with `tar | ssh`, build with the base clang, run ctest.

**Why:** cloud-init avoids driving the serial console; KVM needs the user in the `kvm` group (WSL: then `wsl
--shutdown`, which also empties `/tmp`).
**How to apply:** FreeBSD's pkg has only cmake 3.31 (quarterly and latest, 2026-10-26), so sppc's minimum was lowered
from 4.1 to 3.31 rather than build cmake in CI; don't raise it past what FreeBSD's pkg has. CI runs the same in
`vmactions/freebsd-vm` (`build-freebsd` in `_build.yaml`). `ktrace -i` + `kdump` finds a hung test's last syscall.
