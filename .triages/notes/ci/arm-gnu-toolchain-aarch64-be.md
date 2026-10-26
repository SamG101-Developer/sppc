---
name: arm-gnu-toolchain-aarch64-be
description: "aarch64_be builds with Arm's own GNU toolchain (Ubuntu ships none), fetched from gitlab.arm.com and SHA-pinned"
updated: 2026-10-26
---

Ubuntu and Debian package no `gcc-aarch64_be-linux-gnu`, so `_build.yaml` marks that target `toolchain: arm-gnu` and
downloads Arm GNU Toolchain's `aarch64_be-none-linux-gnu` tarball (pinned URL + sha256; 15.3.rel2 on 2026-10-26). Its
glibc is inside the tarball, so qemu's `-L` is the compiler's `-print-sysroot`, exported as `SYSROOT`, not
`/usr/<triple>`. Releases moved: developer.arm.com redirects to `gitlab.arm.com/tooling/gnu-toolchains-for-arm`, whose
README lists the release branches; each branch's README has the package-registry links and `.sha256asc`. The old
`developer.arm.com/-/media/Files/downloads/gnu/<v>/binrel/` URLs stop at 15.2.rel1.

**Why:** an apt-style lookup finds nothing newer than 15.2.rel1 and looks like the latest.
**How to apply:** when bumping, read the GitLab README's release table, take the newest, and update `ARM_GNU` and
`ARM_GNU_SHA256`. Locally, it unpacks to `/opt/arm-gnu-toolchain-<v>-x86_64-aarch64_be-none-linux-gnu`.
