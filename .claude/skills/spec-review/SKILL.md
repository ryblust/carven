---
name: spec-review
description: Review changed C++ code against cpp-design.md and cpp-format.md specs.
---

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
4. Consolidate output into three sections:

```
## Conflicts
- [file:line] rule — `current` → `expected`

## Style
- [file:line] rule — `current` → `expected`

## Spec gaps
- pattern not covered by either spec
```
