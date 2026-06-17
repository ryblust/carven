---
name: spec-review
description: Final explicit spec compliance review for Carven C++ changes against .agents/specs/cpp-design.md and .agents/specs/cpp-format.md. Use only when the user explicitly asks for spec-review, spec compliance review, or a pre-commit spec pass; do not use for ordinary review.
---

# Spec Review

Perform a final compliance pass after ordinary correctness and architecture review. Report only concrete spec deviations; do not edit files.

## Workflow

1. Use the user-provided scope, or inspect staged and unstaged changes by default.
2. Read changed C++ files (`.cppm`, `.cpp`, `.h`, `.hpp`) and relevant diff hunks.
3. Read `.agents/specs/cpp-design.md` and `.agents/specs/cpp-format.md` completely.
4. Read `AGENTS.md` only when non-C++ changes encode repository contracts.
5. Check changed C++ against both specs. Prefer exact rule conflicts over broad preferences.
6. Merge duplicates, omit compliant items, and keep the report short.
7. Do not flag anything without a concrete spec rule or a concrete spec feedback point behind it.

## Output

Lead with findings. Use repository-relative paths and one actionable location per finding. Use one format for both passing and failing reviews.

```markdown
## Spec Review

Result: <no conflicts found | N finding(s)>
Scope: <files, diff, or user-provided scope>

| Kind | Location | Rule / Area | Finding | Action |
| --- | --- | --- | --- | --- |
| Conflict | `file:line` | <spec rule> | <what the change does> | <what the spec requires> |
| Style | `file:line` | <spec rule> | <current form> | <required form> |
| Spec Feedback | `pattern` | <missing, inconsistent, or awkward spec coverage> | <why the spec is hard to apply> | <clarify spec or adjust design> |

Residual risk: <tests not run, generated output not reviewed, or scope limitation>
```
