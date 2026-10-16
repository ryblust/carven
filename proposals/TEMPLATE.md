# <Proposal title>

- **Status:** Exploration | Draft | Accepted | Deferred | Superseded
- **Implementation:** <Not started | In progress | Partial | Complete; only for a selected delivery scope>
- **Scope:** <design domain>
- **Depends on:** <whole-proposal prerequisites; omit when there are none>

Use only fields and sections that contain useful information. Status describes
design maturity. Record implementation progress only against a selected contract
and completion evidence; omit it for an exploration without a selected delivery.
Deferred status needs a reactivation condition; Superseded names the replacement.
Write concise, factual, neutral prose. Preserve useful alternatives,
counterexamples, and rationale; omit rhetorical defenses and task history.

## Summary

State the problem, intended outcome, and remaining work in a few paragraphs.
For a mixed-maturity domain, a compact table can identify each slice's maturity
and next decision.

## Context

Explain the current behavior and constraints needed to understand the design.
Keep the proposal understandable without following references.
Keep only the current constraints needed to understand unfinished decisions.
Remove completed design sections once permanent documentation covers them.
An implemented prerequisite is context, not an unfinished implementation slice.
Name the remaining extension precisely when a neighboring mechanism already
exists.

## Goals and non-goals

List intended outcomes and adjacent concerns outside the scope. Omit this
section when the summary and design already establish these boundaries.

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

Order questions by actual dependency. Put dependencies on the specific scope
that consumes them and explain which contract they supply.

### OPEN-01 — <concrete question>

- **Question:** <choice to make and its observable consequence>
- **Constraints:** <rules the answer must preserve>
- **Options:** <known alternatives and tradeoffs>
- **Closure condition:** <decision or evidence needed>

Omit fields whose information is already clear. If alternatives are unknown,
state what example or evidence will identify them. For blocked work, name the
dependency and the condition that makes the question actionable.

## Deferred work

### DEFER-01 — <independently reactivatable direction>

- **Reason deferred:** <why it is outside the active scope>
- **Depends on:** <prerequisites; omit if none>
- **Reactivation condition:** <observable need and evidence>

Retain the technical questions and evidence needed to reconsider the direction.

## Implementation

For a selected delivery, describe its contract, necessary dependencies, source,
compiler, runtime, and documentation work. State its completion evidence and any
unresolved decisions that block it. An exploration can instead describe the
bounded experiment needed to choose a design. Omit unrelated follow-up work.

## Validation

State the evidence required: source examples, rejected programs and diagnostics,
semantic edge cases, generated-C++ checks, interoperability tests, or measurements.
Tie experiments to the specific decision or claim they test.

## References

Collect relevant current contracts, related designs, and research sources here.
Group them by subject if needed. Use inline citations only where essential to
understanding the statement. Omit this section when there are no useful references.
