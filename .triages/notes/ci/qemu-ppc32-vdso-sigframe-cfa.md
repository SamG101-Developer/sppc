---
name: qemu-ppc32-vdso-sigframe-cfa
description: "qemu-user's ppc32 vdso puts the rt signal frame's CFA 16 bytes low, so unwinding stops at the trampoline; still in qemu master"
updated: 2026-10-26
---

qemu's `linux-user/ppc/vdso.S` gives `__kernel_sigtramp_rt32` the CFA `r1 + SIGNAL_FRAMESIZE +
offsetof_rt_sigframe_mcontext` (384), but `signal.c` places the 32-bit rt frame `RT_SIGFRAME_ADJUST` (16) higher, as
the kernel does: the mcontext is really at `r1 + 400`. libgcc reads the registers 16 bytes off and the walk ends at
`linux-vdso32.so.1`, so a fault's backtrace never reached the faulting frame (seen 2026-10-26 on 10.0.13 and 11.1.2,
and in qemu master). A real kernel's vdso is right. 64-bit ppc is unaffected (no adjust).

**Why:** it looked like a broken `ucontext` read or context switch in the ppc32 port.

**How to apply:** `_sppc_bt_print` now prints the fault pc alone when the unwinder never reaches it, which keeps
`test_process` green under qemu. Don't "fix" the ppc32 port for this; drop this note once qemu adds the adjust.
Tracked as U18 in spp's `.triages/upstream-watch.md`; the qemu patch and issue text are in spp's `.triages/tickets/`.
CI's qemu, our fork's release ([[qemu-from-spp-fork]]), carries the fix; the fallback stays for any unwinder that stops short.
