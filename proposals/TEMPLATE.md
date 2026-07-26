# <Proposal title>

- **Status:** Exploration | Draft | Accepted | Deferred — <reactivation condition> | Superseded — <replacement>
- **Implementation:** Not started | In progress | Partial | Complete | Not applicable
- **Scope:** <the design domain owned by this proposal>
- **Depends on:** None | <whole-proposal prerequisites>

Use the sections that contain useful information and keep them in the order
shown here. Omit empty sections. Distinguish current repository behavior,
settled but unimplemented design, and candidates throughout the document.

## Summary

State the problem, intended outcome, current maturity, and scope boundary in a
few paragraphs. For a mixed-maturity domain, add a compact map:

| Slice | Maturity | Current frontier |
| --- | --- | --- |
| <design slice> | Accepted | <stable decision or implementation handoff> |
| <design slice> | Exploration | <first open decision> |
| <design slice> | Deferred | <DEFER identifier or reactivation condition> |

## Context

Give the current facts and terminology needed to understand the proposal.
References provide evidence but do not replace this explanation.

## Goals and non-goals

### Goals

- <outcome this proposal intends to establish>

### Non-goals

- <adjacent concern outside this proposal>

These lists define the proposal boundary, not an implementation task list.

## Design

Present the coherent design rather than a history of the discussion. For a
language feature, move from the user model and observable semantics to compiler
facts and target realization. Label unsettled examples and candidates where
they appear.

For mixed-maturity work, mark each design slice explicitly, for example:

**Maturity:** Accepted semantics; source spelling remains open.

## Decision record

Record consequential settled choices under stable identifiers. The complete
design belongs above; this section is an index with concise rationale.

| ID | Decision | Design | Rationale |
| --- | --- | --- | --- |
| `DEC-01` | <settled choice> | [Owning section](#design) | <reason> |

## Open decisions

Order open questions by dependency and name the first unblocked item:

**Next discussion:** `OPEN-01`

### OPEN-01 — <one concrete design question>

- **Status:** Active | Blocked
- **Depends on:** None | <prerequisites>
- **Blocked by:** None | <unsatisfied prerequisite>
- **Activation condition:** Active now | <event that clears the block>
- **Why it matters:** <observable consequence>
- **Constraints:** <facts an answer must preserve>
- **Options:** <known choices and tradeoffs, or Unknown>
- **Closure condition:** <decision or evidence needed>

`Depends on` orders related decisions. `Blocked by` names an unmet prerequisite.
`Activation condition` names the observable event that makes a blocked decision
active. When the design space is not yet known, record `Options: Unknown` and
the evidence needed to identify candidates.

## Deferred work

Use one stable identifier for each independently reactivatable direction.

### DEFER-01 — <inactive direction>

- **Reason deferred:** <why it is outside the active frontier>
- **Depends on:** None | <prerequisites>
- **Reactivation condition:** <observable condition>

Candidate tables, research questions, and collected evidence may follow these
fields when they help a later reader resume the work.

## Implementation

Describe delivery boundaries, dependency order, and permanent-document handoff
after the design is actionable. Keep unsettled design in `Open decisions` and
inactive directions in `Deferred work`. Avoid schedules, assignees, test counts,
and private task queues.

## Validation

State the evidence needed at the proposal's current maturity: source examples,
rejected programs and diagnostics, semantic edge cases, generated-C++ checks,
interoperability tests, measurements, or experiments that close open decisions.

## References

- <permanent documentation, implementation, tests, related proposals, notes, or primary sources>

The proposal remains the self-contained design record; references provide
supporting evidence.
