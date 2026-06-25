---
name: spec-review
description: Final explicit spec compliance review for Carven C++ changes against .agents/specs/cpp-design.md, .agents/specs/cpp-format.md, and .agents/specs/cpp-layout.md. Use only when the user explicitly asks for spec-review, do not use for ordinary review.
---

## Workflow

1. **Preread** — read all three spec files: `.agents/specs/cpp-design.md`, `.agents/specs/cpp-format.md`, and `.agents/specs/cpp-layout.md`.
2. **Scope** — count total diff lines to determine the method:
   - **≤500 diff lines** → Direct Review
   - **>500 diff lines** → Sub-agent Review
3. **Review** — produce a draft report:
   - **Direct Review**: Read all changed C++ files and diff hunks. Check each against all three specs — flag exact rule violations only, deduplicate by rule + location, cite a concrete rule per finding. Output the standard report.
   - **Sub-agent Review**: Batch changed files; spawn one sub-agent per batch (parallel). Each returns per-file findings, cross-file observations, and a per-file summary. Collect, deduplicate across all sub-agents, then cross-check batches for type mismatches, broken naming patterns, and calling convention mismatches. Output the standard report.
4. **Verify** — spawn a sub-agent with all three spec files and the draft report to validate all findings. It removes false positives, merges duplicates, and flags missing rule citations, then passes the corrected report to the main agent for finalization.

---

## Report Format

```markdown
## Spec Review

Result: <N finding(s) | no conflicts found>
Scope: <files, diff>
Method: direct | sub-agent (N sub-agents)

### Spec Compliance

| Kind | Location | Rule / Area | Finding | Action |
| --- | --- | --- | --- | --- |
| Conflict | `file:line` | <spec rule> | <what the change does> | <what the spec requires> |
| Spec | `file:line` | <spec rule> | <current form> | <required form> |
| Gap | `pattern` | <spec gap> | <why the spec is hard to apply> | <clarify spec or adjust design> |

Residual risk: <tests not run, generated output not reviewed, or scope limitation>
```
