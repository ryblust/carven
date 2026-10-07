# Proposals

This directory holds unfinished designs and accepted work awaiting implementation.
Current contracts belong in `docs/`.

## Contents

- [TEMPLATE.md](TEMPLATE.md): proposal structure and field guidance.
- [design-directions.md](design-directions.md): candidates without standalone proposals, including
  multi-field consuming decomposition, richer failure payloads, and infrastructure.
- Other Markdown files: one design domain's unfinished work, keeping related
  syntax, semantics, compiler representation, and lowering together.

## Design ownership

| Proposal | Decision owned here |
| --- | --- |
| [Generic extensions](generic-extensions.md) | Static capabilities, associated types, parameterized class operations, and finite boundary extensions |
| [Operator capabilities](operator-capabilities.md) | Mapping existing tokens to checked operations; consumes generic evidence |
| [Dynamic values](dynamic-values.md) | Erased holding forms, nominal conformance, and dispatch |
| [Constant storage](constant-storage.md) | Constant-execution admission for library storage and retention of completed values and reference graphs |
| [Semantic execution](semantic-execution.md) | Structured static roots, object and literal identity, and address observations during execution |
| [Compiler optimization](compiler-optimization.md) | Compiler costs and C++ realization experiments preserving existing source contracts |
| [Data representation](data-representation.md) | Record and enum representations, native-layout boundaries, compact handles, and collection-layout experiments |
| [Uninitialized storage](uninitialized-storage.md) | Construction destinations, partial initialization, cleanup, native object lifetime, and caller validity obligations |
| [Formatting and output](formatting-and-output.md) | Capacity and composition across text/output observation boundaries |
| [Concurrency](concurrency.md) | Cross-thread values, memory ordering, threads, and synchronization |
| [Documentation comments](doc-comments.md) | Source attachment, retained content, and its artifact consumer |

Implemented async operations, tuples, and the parametric generic core are covered
by the language and compiler references. Undeveloped async extensions belong in
design directions; product traversal and representation experiments stay with generics
and compiler optimization respectively.

## Status

Design maturity and implementation progress are recorded separately.

| Design status | Meaning |
| --- | --- |
| Exploration | The problem and viable choices are being established. |
| Draft | A design exists with required choices still open; slices may differ in maturity. |
| Accepted | Required active decisions are closed and implementation can proceed. |
| Deferred | Work is paused with a reason and reactivation condition. |
| Superseded | A named proposal replaces the design. |

Implementation uses `Not started`, `In progress`, `Partial`, `Complete`, or
`Not applicable`. Record whole-proposal status at the top and slice-specific
status with the slice.

Exploration records questions, alternatives, and counterexamples. Implementation
scope is selected separately, with a defined contract and validation criteria.

## Maintenance

- Describe behavior, rationale, and constraints in concise, factual, neutral prose.
  Distinguish current behavior, accepted design, candidates, and provisional syntax.
  State limits directly; omit rhetorical defenses and repeated scope disclaimers.
- Preserve design reasoning, methods, practical guidance, and validation criteria
  when condensing text. Keep accepted decisions and identifiers stable; a changed
  decision explicitly supersedes its predecessor.
- Keep each rule in its design section. Order open questions by dependency and
  give independently deferred directions separate identifiers and reactivation
  conditions. Omit empty sections, redundant fields, and personal task queues.
- State each necessary dependency at the scope that consumes it and explain the
  contract it supplies. Keep unrelated prerequisites, follow-up commitments, and
  historical compatibility obligations out of the selected scope.
- Make the design understandable on its own. Collect related designs and research
  sources under `References` at the end. Keep inline citations only where essential.
- Retain useful reasoning, counterexamples, and source references. Omit completed
  task lists, experiment logs, and superseded implementation detail.
- Give distinct semantic domains separate owners. Related implementation
  experiments can share a document with independent scopes, decisions, cost
  measures, and validation; split them when a developed design needs its own owner.
- Remove a proposal once implementation and permanent documentation are complete.
  For mixed scopes, retain a short implemented foundation and the unfinished work.
  Consolidate candidates without a developed design into design directions, retaining
  relevant constraints, activation conditions, and validation requirements.
  Update inbound links when moving or removing material.
  Keep the remaining document self-contained.

## References

- [Current contracts](../docs/README.md)
