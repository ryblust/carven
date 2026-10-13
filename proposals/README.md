# Proposals

This directory holds unfinished designs and accepted work awaiting implementation.
Current contracts belong in `docs/`.

The remaining source contract determines each proposal's scope. Implemented
foundations are listed separately.

## Contents

- [TEMPLATE.md](TEMPLATE.md): proposal structure and field guidance.
- [example-polish-implementation-plan.md](example-polish-implementation-plan.md):
  cross-domain delivery plan for the three examples, standard-library additions,
  native bridging, and coordination with the Carven Xmake rules repository.
- Other Markdown files: one design domain's unfinished work, keeping related
  syntax, semantics, compiler representation, and lowering together.

## Design ownership

| Proposal | Decision owned here |
| --- | --- |
| [Generic extensions](generic-extensions.md) | Static capabilities, associated types, parameterized class operations, and finite boundary extensions |
| [Sequence views and pipeline calls](sequence-pipelines.md) | Pipeline call composition, bounded lazy Read sequence APIs, traversal, and static execution |
| [Operator capabilities](operator-capabilities.md) | Mapping existing tokens to checked operations; consumes generic evidence |
| [Dynamic values](dynamic-values.md) | Erased holding forms, nominal conformance, and dispatch |
| [Dynamic ownership](dynamic-ownership.md) | Unique/shared object owners, allocator policy, and factory construction in owned storage |
| [Semantic execution](semantic-execution.md) | Direct structured initializer/extent roots, scalar text traversal, address observations, and retained reference graphs |
| [Compiler optimization](compiler-optimization.md) | Compiler and workspace-reuse costs, C++ realization, and formatting/output experiments preserving source contracts |
| [Frontend analysis](frontend-analysis.md) | Recovering syntax, partial semantics, diagnostic ownership, query coordination, publication, and analysis fault isolation |
| [Data representation](data-representation.md) | Record, tuple, and enum representations, native-layout boundaries, compact handles, and collection layouts |
| [Uninitialized storage](uninitialized-storage.md) | Construction destinations, partial initialization, native lifetime, and library storage in constant execution |
| [Concurrency](concurrency.md) | Cross-thread values, memory ordering, threads, and synchronization |
| [Documentation comments](doc-comments.md) | Source attachment, retained content, and its artifact consumer |

## Implemented boundaries and remaining work

No standalone proposal is fully implemented. Workspace reuse experiments share
the compiler-optimization owner; frontend analysis owns their semantic query and
worker prerequisites. The other domains retain distinct source contracts or
developed representation and validation criteria.

| Domain | Implemented boundary | Remaining scope |
| --- | --- | --- |
| Generic extensions | [Type/failure-set parameters](../docs/language/types.md#nominal-type-parameters), checked named generic functions, static inputs, finite first-order formation, and parameterized native type applications | Capability evidence, associated types, parameterized class operations, and generic `import(cpp)` / `export(cpp)` declarations |
| Sequence views and pipeline calls | Integer ranges, Read slices, concrete closures, fixed iterable families, and inferred function results | Pipeline syntax, five sequence builtins, lazy views including Vector elements, traversal checking, and execution |
| Operator capabilities | Builtin operators and compiler-selected equality | User-defined token hooks and their identity/signature/evidence rules |
| Dynamic values | [Closures and callable views](../docs/language/functions.md), fixed-signature indirect calls, operation values | Nominal erased holding, conformance, existential packaging, and dispatch |
| Dynamic ownership | [String](../docs/language/text.md), [Vector](../docs/language/aggregates.md#owning-vectors), and async owners | General Unique/Shared owners, allocation policy, and factory construction |
| Semantic execution | [Structured executor](../docs/compiler/analysis/evaluation.md), specialization, compound constants, and byte traversal | Direct structured initializer/extent roots, scalar traversal, address/literal policy, and reference graphs |
| Compiler optimization | [Prepared formatting and output](../docs/compiler/backend/builtins.md), [stable-owner Read traversal](../docs/compiler/backend/preparation.md#body-summaries-and-storage-observations), known-value paths, [workspace caches](../tools/workspace/README.md), shared native library producers | Further measured reuse, parallelism, storage/delivery, constant output, and formatting experiments |
| Frontend analysis | Top-level recovery, owned complete analysis, retained snapshots, limited observations after semantic errors, strict publication | Lossless damaged syntax, independent partial types, semantic queries, request control, and worker isolation |
| Data representation | Declaration-order records, recursive tuples, variant-based enums/failures | Target layout facts, native forms, field ordering, compact carriers, and collection representations |
| Uninitialized storage | [Native direct/delayed construction](../docs/compiler/backend/realization.md) | Source destinations, completion evidence, partial initialization, and library cleanup ownership |
| Concurrency | [Same-thread async](../docs/language/async.md) and explicit native boundary obligations | Cross-thread admission, memory ordering, threads, and synchronization |
| Documentation comments | Retained source bytes and ordinary comments | Documentation recognition, attachment, markup, and an artifact consumer |

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
status with the slice. Scope implementation status to the remaining design;
identify implemented foundations separately. Accepted decisions do not establish
implementation, and an implemented neighboring mechanism does not complete a
proposal's different source or tooling contract.

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
- Remove completed sections once implementation and permanent documentation are
  complete; delete proposals with no unfinished scope. Keep only the current
  constraints needed to understand remaining decisions.
  Keep deferred constraints with the design that consumes them; omit speculative
  feature inventories without a concrete problem or decision.
  Update inbound links when moving or removing material.
  Keep the remaining document self-contained.

## References

- [Current contracts](../docs/README.md)
