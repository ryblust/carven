# Proposals

This directory holds unfinished designs and accepted work awaiting implementation.
Current contracts belong in `docs/`; general mechanisms and research belong in
`notes/`.

## Contents

- [TEMPLATE.md](TEMPLATE.md): proposal structure and field guidance.
- [roadmap.md](roadmap.md): candidates without standalone proposals, including
  multi-field consuming decomposition, richer failure payloads, and infrastructure.
- Other Markdown files: one design domain's unfinished work, keeping related
  syntax, semantics, compiler representation, and lowering together.

## Design ownership

| Proposal | Decision owned here |
| --- | --- |
| [Generics](generics.md) | Type parameters, definition-site checking, canonical static evidence and instances |
| [Tuples](tuples.md) | Builtin heterogeneous products, element access, decomposition, and ownership |
| [Operators](operators.md) | Mapping existing tokens to checked operations; consumes generic evidence |
| [Dynamic values](dynamic-values.md) | Erased holding forms, nominal conformance, and dispatch |
| [Constant storage](constant-storage.md) | Constant-execution admission for library storage and retention of completed values and reference graphs |
| [Execution model](execution-model.md) | Structured static roots, object and literal identity, and address observations during execution |
| [Compiler optimization](compiler-optimization.md) | Compiler costs and C++ realization experiments preserving existing source contracts |
| [Layout](layout.md) | Target field order, pointer-plus-state representation, and native-layout boundaries |
| [Uninitialized storage](uninitialized-storage.md) | Construction destinations, partial initialization, cleanup, native object lifetime, and caller validity obligations |
| [Formatting](formatting.md) | Capacity and composition across text/output observation boundaries |
| [Async](async.md) | Suspension, cancellation, and structured operation lifetime |
| [Concurrency](concurrency.md) | Cross-thread values, memory ordering, threads, and synchronization |
| [Documentation comments](doc-comments.md) | Source attachment, retained content, and its artifact consumer |

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
  Consolidate candidates without a developed design into the roadmap, retaining
  relevant constraints, activation conditions, and validation requirements.
  Update inbound links when moving or removing material.
  Keep the remaining document self-contained.

## References

- [Current contracts](../docs/README.md)
- [Research notes](../notes/README.md)
