# Proposals

This directory holds unfinished designs and accepted work awaiting implementation.
Current contracts belong in `docs/`.

Each proposal owns a distinct unfinished contract or design question. Current
mechanisms appear only where they explain that remaining scope.

## Contents

- [TEMPLATE.md](TEMPLATE.md): proposal structure and field guidance.
- Other Markdown files: one design domain's unfinished work, keeping related
  syntax, semantics, compiler representation, and lowering together.

## Design ownership

| Proposal | Decision owned here |
| --- | --- |
| [Generic extensions](generic-extensions.md) | Source-defined static capabilities, associated types, and finite boundary extensions |
| [Sequence views, methods, and pipeline calls](sequence-pipelines.md) | Shared sequence method/free-function contracts, explicit-access pipes, lazy Read views, traversal, static execution, and joint delivery scope |
| [Synchronous generators](generators.md) | Generator production, suspension, ownership, failure, early destruction, const execution, and shared one-shot method/free-function composition |
| [Operator capabilities](operator-capabilities.md) | Mapping existing tokens to checked operations, canonical evidence, and operand/result contracts |
| [Dynamic values](dynamic-values.md) | Erased holding forms, nominal conformance, and dispatch |
| [Dynamic ownership](dynamic-ownership.md) | Unique/shared and native resource owners, allocator policy, construction in owned storage, and move-only Vector capabilities |
| [Semantic execution](semantic-execution.md) | Interpreted closures, additional address observations, and retained reference graphs |
| [Native project queries](project-builds.md) | Shared target declarations, project queries, declaration refresh, and generated-input publication |
| [Compiler optimization](compiler-optimization.md) | Compiler and workspace-reuse costs, C++ realization, and formatting/output experiments preserving source contracts |
| [Frontend analysis](frontend-analysis.md) | Lossless damaged syntax, snapshot-owned partial semantics, query coordination, cancellation, and analysis fault isolation |
| [Data representation](data-representation.md) | Record, tuple, and enum representations, native-layout boundaries, compact handles, and collection layouts |
| [Uninitialized storage](uninitialized-storage.md) | Construction destinations, partial initialization, native lifetime, and library storage in constant execution |
| [Concurrency](concurrency.md) | Cross-thread values, memory ordering, threads, and synchronization |
| [Structured async](structured-async.md) | Finite runtime-sized operation composition and cleanup |
| [Documentation comments](doc-comments.md) | Source attachment, retained content, and its artifact consumer |

## Capability boundaries

Separate public API ownership, semantic knowledge, and implementation when
selecting a delivery:

| Responsibility | Boundary |
| --- | --- |
| Builtins | Compiler-owned types or operations with bounded source contracts |
| Compiler knowledge | Facts used to check and realize type, access, lifetime, failure, and execution contracts, including ordinary source operations |
| Runtime Craft | Shared native representations and execution mechanisms needed by generated programs |
| Standard Craft | Public source declarations, reusable source implementations, domain policy, and capability-specific native adapters |

One capability can use all four. A standard-module API may be builtin; a native
implementation may belong to its Craft. Internal capability facts need public
syntax or source impl evidence only when an API requires users to express them.

State the contract supplied by each prerequisite. Compare a bounded builtin
with a general mechanism using concrete consumers and total language, compiler,
library, and runtime costs. General source-language mechanisms need independent
source uses; rewriting an existing builtin alone does not select them. Internal
abstractions are evaluated against their compiler, runtime, or tooling consumers.
Shared checking and publication retain one semantic authority.

Select delivery slices separately from the design's expressiveness. Record
which uses a slice supports and the evidence it needs. Keep unresolved and
deferred directions with their decision criteria; a smaller delivery establishes
neither a permanent restriction nor a commitment to a particular extension.

## Status

Design maturity and implementation progress are recorded separately.

| Design status | Meaning |
| --- | --- |
| Exploration | The problem and viable choices are being established. |
| Draft | A design exists with required choices still open; slices may differ in maturity. |
| Accepted | Required active decisions are closed and implementation can proceed. |
| Deferred | Work is paused with a reason and reactivation condition. |
| Superseded | A named proposal replaces the design. |

Record `Implementation` only for a selected delivery scope with a defined
contract and completion evidence. Use `Not started`, `In progress`, `Partial`,
or `Complete`, at the scope they describe. `Partial` means part of that same
retained delivery is implemented; an implemented prerequisite or neighboring
mechanism is context, not progress on a different contract.

An exploration without a selected delivery omits implementation status. Design
acceptance does not establish implementation. Remove completed scope after its
current contract is covered by permanent documentation.

## Maintenance

- Use these second-level headings in this order: `Summary`, `Context`, `Design`,
  `Decision record`, `Open decisions`, `Deferred work`, `Implementation`,
  `Validation`, `References`. Always include `Summary` first, stating the problem,
  intended outcome, and unresolved work from existing content. Include `Context`
  only for necessary background beyond the summary; omit other sections without
  substantive content. Keep the remaining names and order. Put specific topics at
  level three or below; use sentence case for titles and headings. This structure
  applies to proposals; this index retains its navigation and maintenance sections.
- Describe current behavior factually. State design rules and implementation steps
  directly; label candidates and provisional syntax. Keep explanations concise,
  neutral, and specific to the decision or mechanism they explain.
  Replace rhetorical defenses with the applicable rule, consequence, or evidence.
- Preserve design reasoning, methods, practical guidance, and validation criteria
  when condensing text. Keep accepted decisions and identifiers stable; a changed
  decision explicitly supersedes its predecessor.
- Keep each rule in its design section. Order open questions by dependency and
  give independently deferred directions separate identifiers and reactivation
  conditions. Omit empty sections, redundant fields, and personal task queues.
- State each necessary dependency at the scope that consumes it and identify the
  contract it supplies. Scope compatibility requirements to a named source or
  native interface. Separate selected delivery requirements from candidate and
  deferred directions. Retain their decision criteria; omit unrelated migration
  obligations and delivery promises.
- Make each proposal self-contained. State the required dependency contract
  locally. Keep document navigation in this index; use cross-document references
  only when essential detail cannot be stated concisely in place. Collect external
  research sources under `References` at the end.
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
