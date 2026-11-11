---
name: branch-protection
description: "AArch64 ELF builds with -mbranch-protection=bti+pac-ret; the switch .S needs its own bti c + property note; BTI/PAC faults reported as CFI failures without a stack walk"
updated: 2026-11-11
---

On AArch64 ELF (Linux, FreeBSD) sppc is built with `-mbranch-protection=bti+pac-ret` (`CMakeLists.txt`, only where the
compiler accepts it, never on Apple; not `standard`, which newer gcc widens to GCS, the shadow stack the green-thread
switch cannot keep), so `libsppc.so` has landing pads, signed return addresses, and the GNU property telling the
loader to guard it (spp S10.1, S10.3). The linker sets that property only when **every** object has it, and an
assembly file gets none from the flag: `headers/sppc/asm/switch_aapcs64.S` emits its own `bti c` and
`.note.gnu.property` (the AArch64 feature word: BTI bit 0, PAC bit 1) from `__ARM_FEATURE_BTI_DEFAULT` and
`__ARM_FEATURE_PAC_DEFAULT`. The switch signs nothing: the return address it saves and restores is its caller's, as
`bl` left it, and each task resumes on its own stack, so its callers' signatures still authenticate. Check with
`readelf -n libsppc.so` ("AArch64 feature: BTI, PAC").

The crash handler reports each as a failed control-flow-integrity check, like a KCFI trap (`backtrace/posix.h`):
a BTI fault (SIGILL with the saved `BTYPE`, `pstate` bits 10-11, set: `_sppc_bt_is_bti_fault` in `elf.h`), and a
failed authentication (SIGILL at an `autiasp`/`autibsp`/`retaa`/`retab`, which only FEAT_FPAC raises:
`_sppc_bt_is_pac_fault`; without FPAC the return to the poisoned address is an ordinary SIGSEGV, as indistinguishable
from any wild jump). Neither walks the stack (`_sppc_bt_print(..., walk = false)`, the faulting frame only): a branch
past a prologue leaves the return address unsigned where the unwind tables say it is signed, and libgcc's unwinder
authenticates what it reads, so it faulted again inside the handler. qemu-user `-cpu max` has BTI and FPAC and keeps
`BTYPE` in the signal frame, so all of it is tested under it.

CI checks the property on both AArch64 ELF targets (`_build.yaml`, "Check the branch protection property") and, when
it is missing, relinks with `-z force-bti`, whose warnings name each input that lacks it. Added 2026-11-11 after the
first release from `ubuntu-24.04-arm` shipped without it although the code had its pads and signatures: one of that
runner's own link inputs lacks the property.

**Why:** a library without the property is left unguarded, silently; the same happens to any new `.S` without the note.
**How to apply:** a new AArch64 `.S` gets `bti c`/`bti j` where it can be branched to, signs any return address it
saves, and the property note. Cross-build and test with `CC=aarch64-linux-gnu-gcc cmake -S . -B <dir>
-DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64
"-DCMAKE_CROSSCOMPILING_EMULATOR=qemu-aarch64;-cpu;max;-L;/usr/aarch64-linux-gnu"`, then `ctest`.
