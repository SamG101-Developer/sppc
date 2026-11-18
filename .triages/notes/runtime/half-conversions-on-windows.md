---
name: half-conversions-on-windows
description: "sppc exports __extendhfsf2/__truncsfhf2/__truncdfhf2 on x86-64 Windows: LLVM calls them for F16 and the MSVC runtime has none; a half rides in a float's low 16 bits"
updated: 2026-11-18
---

Baseline x86-64 has no half instructions, so LLVM converts an `F16` through compiler-rt libcalls: `__extendhfsf2`
(half to float), `__truncsfhf2` (float to half) and `__truncdfhf2` (double to half). Linux has them in libgcc and macOS
in libSystem; the MSVC runtime has none, so an S++ program using `F16` failed to link on Windows ("undefined symbol:
__truncdfhf2", SPP-STL's conformance tests, 2026-11-18). `headers/sppc/softfloat.h` defines them on x86-64 Windows
only, exported from `sppc.dll`, over portable integer conversions (`_sppc_f64_to_f16_bits`, `_sppc_f32_to_f16_bits`,
`_sppc_f16_to_f32_bits`: one rounding, to nearest even; a NaN keeps its sign and top payload, quiet). AArch64 Windows
converts with `fcvt` and calls none.

On x86-64 LLVM passes and returns a half in XMM0's low 16 bits (checked with `llc -mtriple=x86_64-pc-windows-msvc`:
`pinsrw $0` before the call, `pextrw $0` after). msvc C has no `_Float16`, so each helper takes or returns a `float`,
which the Microsoft x64 convention also puts in XMM0, and moves the 16 bits through `memcpy` (the upper bits coming in
are garbage, so they are masked).

**Why:** the spp maintainer wants `spp.exe` to be the whole install, so compiler-rt's builtins library is not shipped
beside it; sppc already reaches every program through SPP-STL. A full set (F128's `__addtf3` and the rest, float plan L2)
is meant to come from compiler-rt built into spp, not more hand-written helpers here.

**How to apply:** `tests/test_softfloat.c` checks the portable conversions against the compiler's own `_Float16`
(every half both ways, float and double sweeps with their boundaries) where it has one, known values elsewhere, and the
exported helpers on x86-64 Windows. A new libcall LLVM needs on Windows goes beside these, with a check there.
