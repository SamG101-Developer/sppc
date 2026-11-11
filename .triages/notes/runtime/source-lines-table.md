---
name: source-lines-table
description: "crash-report file:line comes from a table spp links into the program (sppc_set_source_lines), not from DWARF; format in backtrace/common.h"
updated: 2026-11-11
---

A crash report names each frame's source lines from a table the spp compiler builds out of the program's DWARF and
links in. `main` hands it over with `sppc_set_source_lines` before anything else runs. The format (version 1) is
defined in `backtrace/common.h` ("SOURCE LINES"), and spp's `spp.codegen.llvm_lines` writes it. Each function is keyed
by the FNV-1a hash of the symbol name that `_sppc_bt_resolve` gives, then each place by its offset into the function.
So the reader is the same short C on every platform, and it parses no object format. Every index is bounds-checked: it
runs in a crash.

**Why:** sppc stays a thin wrapper over the kernel. Vendored libbacktrace was tried for this on 2026-11-11 and dropped:
110 KB more, ELF only, and lost with `strip`.
**How to apply:** a change to the format is a new version, made in spp's `llvm_lines.cpp` in the same change. A table
of an unknown version is ignored, not misread. `tests/test_process.c` builds a table by hand to pin the format. spp
links against `sppc_set_source_lines`, so this lands in SPP-STL before spp merges.
