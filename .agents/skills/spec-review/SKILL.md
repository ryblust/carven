---
name: spec-review
description: Final explicit spec-review pass for changed C++ code against cpp-design.md and cpp-format.md; use only when the user asks for spec-review or a spec compliance review.
---

Use this only as a final compliance check when the user explicitly asks for
`spec-review`, a spec-focused compliance review, or a pre-commit spec pass.
Ordinary `review` requests are not spec-review requests.

Review all changed C++ files against [`cpp-design.md`](../../specs/cpp-design.md) and [`cpp-format.md`](../../specs/cpp-format.md).

## Workflow

1. `git diff --name-only` to list changed `.cppm`/`.cpp`/`.h` files.
2. Scale agents by total diff lines:

| Diff lines | Agents |
|-----------|--------|
| < 500 | 2 (design + format) |
| 500–1500 | 4 (2 design, 2 format, split files) |
| > 1500 | 6+ (split by file, each covers both specs) |

3. Each agent reads their files and checks EVERY rule in their assigned spec. Report deviations only — never modify code.
4. If non-C++ build or tooling files changed, also check the project contracts
   they encode:
   - xmake owns normal build, run, install, generated-file, and module/BMI work.
   - `scripts/install.sh` must not edit shell profiles, mutate PATH, or own
     upgrades/uninstalls.
   - Unit tests must not run xmake subprocesses; compiled smoke tests belong in
     `carven-e2e-test`.
   - Golden files are updated only through `scripts/update-golden.sh`.
5. Consolidate output into three sections:

```
## Conflicts
- [file:line] rule — `current` → `expected`

## Style
- [file:line] rule — `current` → `expected`

## Spec gaps
- pattern not covered by either spec
```
