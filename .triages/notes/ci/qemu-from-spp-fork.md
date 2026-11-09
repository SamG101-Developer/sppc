---
name: qemu-from-spp-fork
description: "CI's qemu-user is a static release of our qemu fork (SamG101-Developer/qemu, spp-patches), pinned by tag and sha256, as in spp"
updated: 2026-11-09
---

`_build.yaml`'s "Install qemu" step downloads `qemu-user-static-<tag>-x86_64.tar.xz` from a release of
`SamG101-Developer/qemu` (binaries under `bin/`), pinned by tag and sha256; spp pins the same release in
`[pin.qemu-user]`. The fork is qemu master plus fixes qemu has not shipped (spp's `.triages/upstream-watch.md`,
U17-U19: the ppc32 and loongarch64 signal-frame unwind, and `getsockopt` of socket timeouts), rebased weekly; a
`spp-*` tag builds, checks and publishes a release. Before the fork (to 2026-10-26) this was the newest plain Debian
`qemu-user` deb; the runner's own 8.2 crashed ppc64le signal unwinding and lost mips socket timeouts.

**Why:** the maintainer's rule is the newest qemu, and the fork is that plus the fixes our tests need.
Since `spp-2026.11.09.3` it also fails `mprotect`/`msync` of unmapped memory with ENOMEM on 32-bit guests
(ppc, arm, mips, mipsel, m68k): qemu keeps an unmapped guest page as a `PROT_NONE` host page inside the guest's
reserved 4 GB, which the host accepts, so `test_mem` saw 0. Two commits on `spp-patches`; the second casts msync's
`abi_long` args to `abi_ulong`, since m68k maps above 2 GB and the address sign-extended. Squash them for upstream.

**How to apply:** move `QEMU_TAG`/`QEMU_SHA256` to the fork's newest release together with spp's pin (spp's
`refresh-pins.sh` reads it). Test locally with that same tarball, unpacked, not the distro's qemu. Once U17-U19 and the
mprotect/msync fix are all in a qemu release, go back to Debian's newest deb.
