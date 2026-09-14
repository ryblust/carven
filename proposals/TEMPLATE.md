# <Proposal title>

- **Status:** Exploration | Draft | Accepted | Deferred | Superseded
- **Implementation:** Not started | In progress | Partial | Complete | Not applicable
- **Scope:** <design domain>
- **Depends on:** <whole-proposal prerequisites; omit when there are none>

Use the sections that contain useful information. Distinguish current behavior,
accepted design, and candidates. Deferred status needs a reactivation condition;
Superseded status names the replacement.

## Summary

State the problem, intended outcome, and remaining work in a few paragraphs.
For a mixed-maturity domain, a compact table can identify each slice's maturity
and next decision.

## Context

Explain the current behavior and constraints needed to understand the design.
Keep the proposal understandable without following references.

## Goals and non-goals

List intended outcomes and adjacent concerns outside the scope. Omit rules
already explained in the design and implementation tasks listed below.

## Design

Describe observable behavior before compiler representation and lowering.
Explain consequential choices where they are introduced. Label unsettled syntax
and candidates, including examples.

## Decision record

Use stable identifiers for consequential accepted choices. Keep the complete
contract in Design; this table is a short summary. Omit it when numbered design
sections already serve that purpose.

| ID | Decision and reason |
| --- | --- |
| `DEC-01` | <choice and concise reason> |

## Open decisions

Order questions by dependency. Identify the next unblocked item, if one exists.

### OPEN-01 — <concrete question>

- **Status:** Active | Blocked
- **Depends on:** <relevant decisions or external prerequisites; omit if none>
- **Activation condition:** <event that clears a block; omit for active work>
- **Question:** <choice to make and its observable consequence>
- **Constraints:** <rules the answer must preserve>
- **Options:** <known alternatives and tradeoffs>
- **Closure condition:** <decision or evidence needed>

Omit a field when its information is already clear. If alternatives are unknown,
state what example or evidence will identify them. Add a separate blocker only
when the dependency and activation condition do not explain it.

## Deferred work

### DEFER-01 — <independently reactivatable direction>

- **Reason deferred:** <why it is outside the active scope>
- **Depends on:** <prerequisites; omit if none>
- **Reactivation condition:** <observable need and evidence>

Retain technical questions needed to resume the design. External reading lists
and experiment histories belong in the archive.

## Implementation

Describe delivery order and the source, compiler, runtime, and documentation
work needed for each accepted slice. Identify unresolved decisions that block it.

## Validation

State the evidence required: source examples, rejected programs and diagnostics,
semantic edge cases, generated-C++ checks, interoperability tests, or measurements.
Tie experiments to the specific decision or claim they test.
