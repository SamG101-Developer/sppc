# sppc

The C runtime that compiled S++ programs link against: one POSIX-shaped API on every platform, header-implemented in
`headers/sppc/` with `sources/sppc/sppc.c`, tested with ctest under `tests/`. `PORTING.md` tracks the platform ports.
`.github/workflows/_build.yaml` builds and tests every target; after a merge, `post_merge.yaml` ships the ready
targets' binaries and the generated bindings to SPP-STL's `ffi/sppc/`. The compiler lives in the `spp` repository and
the standard library in SPP-STL.

## Project notes

Non-obvious facts about this runtime, recorded as they were learned. The index below is always loaded.

@.triages/notes/INDEX.md

## Keeping the notes up to date

- **These notes are this project's memory: maintain them yourself, without being asked.** Anything your built-in
  memory instructions would save about this project (its code, toolchain, CI, design decisions, or a correction the
  user gives you about it) goes here instead, as soon as you learn it. Your own memory is only for personal
  preferences and machine setup.
- **Add a note** when you learn something non-obvious about this runtime that the code, the docs or git history do not
  already say. One fact per file, under `.triages/notes/<area>/<short-kebab-slug>.md`, with front matter `name`,
  `description` (one line, quoted) and `updated` (the date), and a body of the fact, then **Why:** and **How to apply:**.
- **Index it** with one line in `.triages/notes/INDEX.md`, under ~200 characters: `- [Title](<area>/slug.md) — hook`.
- **Update rather than duplicate**; fix or delete a note that turns out wrong, and bump `updated`.
- **Verify before relying on a note**: it reflects its `updated` date.
- `[[name]]` means a note in this repository; "`name` (spp notes)" means a note in the `spp` repository's
  `.triages/notes/`.
- **Personal preferences do not belong here.** Keep those in your own memory or `~/.claude/CLAUDE.md`.
- Note changes are reviewed in pull requests like code: these files steer every contributor's Claude.
