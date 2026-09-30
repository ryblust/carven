# C++ generation

This document describes how published semantics become C++ artifacts through
planning, representation selection, lowering, dependency collection, and emission.
The input is a published semantic program. The representations here describe
the current implementation.

## Pipeline

```text
SemIRProgram + TargetPlanningRequest
  → PlannedCompilation
  → lower_artifact
      → BodyPreparation → fragment realization → target syntax
  → TargetUnit
  → emit
  → GeneratedArtifactSet
```

`PlannedCompilation` owns a sealed semantic program and its matching immutable
`TargetPlan`. Semantic type contents remain owned by `SemIRProgram`.
Planning selects names, interfaces, failure representation, and
artifact schedules. Each artifact is lowered into a fresh `ArtifactLowering`
and target-unit identity. Repeated lowering produces independent units. Type and
signature-result caches belong to that artifact lowering and track incomplete
resolution separately from completed target identities.

`lower_body` prepares a published semantic body and finishes `BodyRealizer` while
that preparation remains alive. `BodyLoweringInputs` supplies parameter identities,
capture member names, and the exit contract; `LoweredBody` returns target statements
and referenced-parameter facts.

`BodyPreparation` borrows the published body's occurrences; the semantic program
outlives preparation and realization. Each realized fragment owns its operation
preparation. [Preparation](preparation.md) defines the summaries, operand demands,
and plans that connect these stages. `BodyRealizer` composes the published
structured regions and preserves their evaluation, storage, and exit contracts.

Within `realization/`, `expr` owns cleanup-frame entry and result delivery;
`fragment` builds and composes completed operands, and `writer` handles repeated
format operands. The `sequencing`, `storage`, and `call` slices preserve execution
order, retain borrowed results, and complete fallible calls. Semantic publication
owns source legality and lifetime contracts; native compilation checks delegated
C++ operations.

`TargetUnitBuilder::finish` verifies the target tree, derives dependencies, and
materializes directives. Rendering serializes that finished tree. Filesystem
output and native compilation are separate consumers.

## Static instances

Each module implementation emits the static instances it calls as `inline`
functions in the owner's module namespace. Instance names derive from the function
and its static values. Native signatures and calls retain only runtime parameters.
Semantic analysis supplies ordinary residual regions with selected static arms,
expanded iterations, and constant values; preparation and realization consume
those regions through their ordinary paths.

An artifact owns module contexts and one definition queue. Each context owns its
type cache and emitted query aliases, display helpers, and constant backing storage.
`TargetNamePlan` owns support names derived from complete semantic content.
Module contexts track materialized constant identities; callable-local allocators
reserve names for locals and labels. Artifact caches use canonical semantic
identities. Content digests select preferred spellings;
naming compares full content when those spellings coincide. Shared bodies reference
module-owned support names. Provider artifacts are independent of the instances selected by their callers;
[Artifacts](artifacts.md#staged-bodies) defines their callable surface and definition
ordering.

Source-template bodies have been discarded before planning. Published closure
construction order supplies stable names and dependency discovery without
recovering source trees from instances.

Semantic specialization gives each expanded iteration distinct local bindings,
patterns, and lifetimes. The backend uses these identities through its ordinary
storage map; references to outer storage remain shared. Normal cleanup, break,
continue, return, and failure handling still apply.

## Reading by task

| Task | Reference |
| --- | --- |
| Select types, constants, parameter policies, or callable/result ABI | [Representation](representation.md) |
| Prepare an operation, classify operand demands, or select a known-value implementation | [Preparation](preparation.md) |
| Preserve evaluation order, snapshots, backing storage, cleanup, or control exits | [Realization](realization.md) |
| Place interfaces, collect dependencies, construct target syntax, or render artifacts | [Artifacts](artifacts.md) |
| Implement String, formatting, printing, assertions, or test reports | [Builtins](builtins.md) |

[Compiler architecture](../README.md) owns semantic construction and
publication. [Language reference](../../language/README.md) owns program validity
and observable behavior. [Toolchain artifacts](../../toolchain/artifacts.md)
defines native compilation and artifact consumption; [CLI](../../toolchain/cli.md)
owns filesystem output policy.

## Extending operations

Semantic analysis publishes the operation's type, evaluation and lifetime contracts.
For ordinary operations using existing contracts, the backend describes ordered
inputs and uses in `preparation/operands.cpp` and realizes C++ syntax in
`realization/operation.cpp`. Inputs with a uniform storage use share the direct
child definition in `semantic.semir.children`; mixed uses retain their operand
adapters. Both visitors enumerate the semantic expression alternatives explicitly.
Native operation realization also enumerates the `CppOperation` alternatives.
Structured control retains its specialized realization paths.

Tests must establish the new operation's behavior and interactions with existing
contracts. Scheduling, storage and cleanup consume those contracts. New control
scopes, ownership modes or partial-object lifetimes require design and checks at
their owning boundaries.

Use [operation preparation](preparation.md#operation-preparation) for selected
implementations and operand mappings, and [evaluation and values](realization.md#evaluation-and-values)
for storage and delivery. Aggregate changes also need the
[sequencing and partial-object boundary](realization.md#aggregate-sequencing).
Text and report operations use these same contracts through the
[builtin implementations](builtins.md).
