# Proposals

This directory holds unfinished designs and accepted work awaiting implementation.
Current language and compiler contracts belong in `docs/`.

## Contents

- [TEMPLATE.md](TEMPLATE.md): proposal structure and field guidance.
- [roadmap.md](roadmap.md): priorities and dependencies across domains.
- Other Markdown files: one design domain's unfinished work, keeping related
  syntax, semantics, compiler representation, and lowering together.

## Design ownership

| Proposal | Decision owned here |
| --- | --- |
| [Generics](generics.md) | Type parameters, definition-site checking, canonical static evidence and instances |
| [Operators](operators.md) | Mapping existing tokens to checked operations; consumes generic evidence |
| [Dynamic values](dynamic-values.md) | Erased holding forms, nominal conformance, and dispatch |
| [Constant storage](constant-storage.md) | Library storage operations during constant execution and retained results |
| [Failure values](failure-value.md) | Payload admission and ownership across throw, matching, and rethrow |
| [Formatting](formatting.md) | Capacity and composition across text/output observation boundaries |
| [Async](async.md) | Suspension, cancellation, and structured operation lifetime |
| [Concurrency](concurrency.md) | Cross-thread values, memory ordering, threads, and synchronization |
| [Documentation comments](doc-comments.md) | Source attachment, retained content, and its artifact consumer |

Dependencies do not transfer ownership of a rule. Operator syntax does not define
generic evidence; async suspension does not define cross-thread sharing; constant
execution does not define a container's public API. Implemented behavior belongs
in `docs/`; the roadmap holds candidate work that has no developed design yet.

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

## Maintenance

- Describe behavior, rationale, and constraints in concise, neutral prose with
  one consistent language. Distinguish current behavior, accepted design, and
  candidates, including provisional syntax.
- Preserve design reasoning, methods, practical guidance, and validation criteria
  when condensing text. Keep accepted decisions and identifiers stable; a changed
  decision explicitly supersedes its predecessor.
- Keep each rule in its design section. Order open questions by dependency and
  give independently deferred directions separate identifiers and reactivation
  conditions. Omit empty sections, redundant fields, and personal task queues.
- Make proposals understandable on their own; link only for necessary contracts
  or evidence. Keep directly relevant research conclusions, and archive reading
  excerpts, source inventories, experiment histories, and discarded prototypes.
  Independent technical articles developed from research may belong in `notes/`.
- Split domains when their parts can be decided and delivered independently.
- Remove a proposal once implementation and permanent documentation are complete.
  For mixed scopes, retain a short implemented foundation and the unfinished work.
  Git history and external archives preserve historical material; repository
  documents must remain usable without access to those archives.
