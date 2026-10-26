---
name: qemu-newest-debian-build
description: "CI runs the newest qemu-user Debian has (static .deb, SHA-pinned in _build.yaml), never the runner's own"
updated: 2026-10-26
---

`_build.yaml` installs qemu-user from Debian's pool (`deb.debian.org/debian/pool/main/q/qemu/`), pinned by URL and
sha256; on 2026-10-26 the newest was `11.1.2+ds-3`. Always move it to the newest build there (the plain one, not a
`~bpo` backport) and test locally with that same extracted binary, not the distro's qemu.

**Why:** the maintainer's rule; the runner's 8.2 crashed ppc64le signal unwinding and lost mips socket timeouts.

**How to apply:** when touching qemu-run targets, list the pool, bump `QEMU_DEB`/`QEMU_SHA256` if newer, and run
ctest with `-DCMAKE_CROSSCOMPILING_EMULATOR=<extracted>/usr/bin/qemu-<arch>;-L;/usr/<triple>`.
