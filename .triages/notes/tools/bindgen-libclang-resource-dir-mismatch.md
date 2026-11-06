---
name: bindgen-libclang-resource-dir-mismatch
description: "Builtin headers from a clang of a different version than libclang hide stdint.h types; tools/main.py picks a matching one"
updated: 2026-11-06
---
`tools/main.py` used to take the compiler builtin headers (stddef.h, stdint.h, ...) from whatever `clang` was on PATH. On a machine where python's libclang is 19 (distro `python3-clang`) but `clang` is 23,
clang 23's `stdint.h` (added with `-isystem`) does `#include_next <stdint.h>`, which lands on libclang 19's own builtin
`stdint.h` (`/usr/include/clang/19.1.7/include/`, always on its search path) rather than glibc's. Both share the guard
`__CLANG_STDINT_H`, so the second is empty and `/usr/include/stdint.h` is never read: every header that uses `uint64_t`
or `intmax_t` fails with "unknown type name". The errors look like a header that isn't self-contained, but `-include stdint.h` doesn't fix them.

**Why:** libclang always searches its own builtin headers too, so a second, different-version builtin dir gets in front of glibc.

**How to apply:** `find_resource_include` now uses no extra dir when libclang finds its own builtin headers (distro
packages), otherwise a clang whose `--version` major matches `clang_getClangVersion`; it warns when `--clang` mismatches.
Keep that version match if you change how the builtin headers are found.
