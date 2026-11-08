---
name: windows-arm64-runner-actions
description: "On windows-11-arm, msvc-dev-cmd needs arch: arm64 (its default x64 sets up a cross compiler), and harden-runner has no agent, so it skips"
updated: 2026-11-08
---

`ilammy/msvc-dev-cmd` defaults `arch` to `x64` whatever the runner's CPU, so on the `windows-11-arm` runner it sets up
the arm64-hosted x64 cross compiler: the build would make x64 binaries that the tests then run under emulation, and
the shipped `aarch64-windows-msvc` folder would hold an x64 `sppc.dll`. `_build.yaml` passes `arch:
${{ matrix.arch || 'x64' }}`, and the arm64 target's matrix row sets `arch: arm64`. `step-security/harden-runner`
(v2.22.2) has no Windows arm64 agent: it logs a message and returns without failing, so that job's egress is not
enforced.

**Why:** both fail quietly. A wrong-arch dll still passes every test, and the hardening step stays green.

**How to apply:** for a new Windows target, set its matrix `arch`, and check the collected `sppc.dll`'s machine
(`dumpbin /headers`: `AA64` for arm64, `8664` for x64). Don't count on harden-runner on Windows arm64 until a release
adds an agent for it.
