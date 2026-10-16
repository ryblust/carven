# <Proposal title>

- **Status:** Exploration | Draft | Accepted | Deferred | Superseded
- **Implementation:** <Not started | In progress | Partial | Complete; only for a selected delivery scope>
- **Scope:** <design domain>
- **Depends on:** <whole-proposal prerequisites; omit when there are none>

Use only metadata fields that contain useful information. Status describes
design maturity. Record implementation progress only against a selected contract
and completion evidence; omit it for an exploration without a selected delivery.
Deferred status needs a reactivation condition; Superseded names the replacement.
Describe current behavior factually and state proposed rules and steps directly.
Use concise, neutral prose. Preserve alternatives, counterexamples, and reasons
that affect a decision. Distinguish selected delivery requirements from exploratory
and deferred work. Omit rhetorical defenses, task history, and unrelated delivery
commitments.

Use the second-level headings below with their exact names and relative order.
Always include Summary first. Include other sections when they have substantive
content. Keep the remaining sections separate; place topic-specific headings at
level three or below. Use sentence case for titles and headings. Keep goals and
scope limits in Summary or Design.

Preserve design reasons, counterexamples, alternatives, and guidance for future work
when reorganizing sections. Keep examples and local validation criteria with the
design they explain; use Validation for evidence across the selected scope.

## Summary

Summarize the problem, intended outcome, and unresolved work from the proposal's
existing content. Introduce no new commitments or repeated background. One or
two sentences suffice for a short proposal. For a mixed-maturity domain, a compact
table can identify each slice's maturity and next decision.

## Context

Include this section when current behavior or constraints need explanation beyond
Summary. Keep that background specific to understanding the design.
Keep the proposal understandable without following references.
Keep only the current constraints needed to understand unfinished decisions.
Remove completed design sections once permanent documentation covers them.
Record implemented prerequisites as context. Name the remaining extension and
its completion evidence separately.

## Design

Describe observable behavior before compiler representation and lowering.
Explain consequential choices where they are introduced. Label unsettled syntax
and candidates, including examples. State compatibility requirements only for
interfaces covered by the design.

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

Collect external research sources here. State required project contracts in the
section that uses them; keep navigation in the proposal index. Use a cross-document
reference only when essential detail cannot be stated concisely in place. Omit
this section when there are no external sources to cite.
