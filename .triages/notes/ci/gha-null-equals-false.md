---
name: gha-null-equals-false
description: "In a GitHub Actions expression an unset matrix key is null, and null == false, so `if: matrix.x != false` skips everywhere"
updated: 2026-11-09
---

GitHub's expression language turns both sides of `==`/`!=` into numbers when their types differ, and both `null` and
`false` become `0`. If a matrix row doesn't set a key, its value is `null`, so `matrix.test != false` is false for that
row. From #15 (`0286bfb`) until 2026-11-09, the Test step used exactly that condition, so ctest was skipped on every
`_build.yaml` matrix target and green runs only showed that the code compiled. musl and FreeBSD were still tested,
because their jobs run ctest without that condition.

**Why:** Nothing fails when this happens. A skipped step shows as 0s in the job's timings, the same as a fast one.
**How to apply:** Don't write `!= false` on optional keys. Use a key whose default is falsy (`if: '!matrix.skip-x'`),
or write `matrix.x == null || matrix.x`. When a step that should always run shows 0s, check its `conclusion` in
`gh api repos/{owner}/{repo}/actions/runs/<id>/jobs`; `skipped` means the condition was false.
