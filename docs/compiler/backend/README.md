# C++ generation

This overview defines the C++ generation pipeline and its ownership boundaries.

## Pipeline

Generation consumes a published semantic program and produces C++ artifacts.

```text
SemIRProgram + TargetPlanningRequest
  → PlannedCompilation
  → lower_artifact
      → BodyPreparation → fragment realization → target syntax
  → TargetUnit
  → emit
  → GeneratedArtifactSet
```

`PlannedCompilation` borrows a sealed program and owns its immutable `TargetPlan`.
Keep the program alive and unmoved through planning and lowering.
`generate_artifacts` borrows it synchronously and returns owned paths, bytes, and
optional implementation origins from semantic module provenance. One program can
serve multiple requests without repeating analysis or static execution.

Planning selects names, interfaces, failure representation, definition placement,
and artifact schedules. It resolves requested library modules once; naming and
artifact planning share those ModuleIDs. Each artifact gets a fresh
`ArtifactLowering`, target-unit identity, type caches, and signature-result caches.
Cache entries distinguish incomplete resolution from completed target identities.
Repeated lowering produces independent units.

`lower_body` prepares the published body, then finishes `BodyRealizer` before
releasing preparation. `BodyLoweringInputs` supplies parameters, capture member
names, and the exit contract; `LoweredBody` returns statements and referenced
parameter facts. `BodyPreparation` borrows semantic occurrences and derives
operation plans and operand demands. Each fragment owns its operation preparation.

Within `realization/`, `expr` owns cleanup frames and result delivery; `fragment`
composes completed operands; `writer` handles repeated format operands.
`sequencing`, `storage`, and `call` preserve execution order, retain backing, and
complete fallible calls. Semantic publication supplies source legality and
lifetime contracts; native compilation checks delegated C++ operations.

`TargetUnitBuilder::finish` collects header requirements and plans directives
from artifact references and ordered native
imports. Runtime symbols, including async facilities, use shared header-provider
metadata. Rendering serializes the finished tree and directives. Filesystem output
and native compilation consume the resulting artifacts separately.

## Realization contracts

Preserve source effects, storage observations, lifetimes, cleanup, and native
conversion capabilities when selecting direct expressions, storage, or control.
Preparation establishes binding stability; realization owns C++ cleanup scopes and
exit receivers; semantic analysis supplies residual pattern coverage. Derive facts
at their owning stage and consume them downstream.

Resolved Carven calls use qualified target names. Import bridges call the native
target retained by semantic analysis. Argument-dependent lookup adds no candidates
to resolved Carven calls.

Static instances arrive as ordinary residual regions with selected static arms,
expanded iterations, and constant values. Their signatures contain only runtime
parameters. Expanded iterations have distinct local identities and share outer
storage. Preparation and realization use their ordinary storage and control paths.

Each artifact owns module contexts and one definition queue. Contexts own type
caches, emitted query aliases, display helpers, and constant backing. The name plan
owns support names derived from semantic content; callable-local allocators reserve
locals and labels. Cache identity uses canonical semantic facts. When preferred
digest-based spellings coincide, naming compares complete content.

## References

| Task | Reference |
| --- | --- |
| Select types, constants, parameter policies, or callable/result ABI | [Representation](representation.md) |
| Prepare operands, operation demands, or known-value implementations | [Preparation](preparation.md) |
| Preserve sequencing, snapshots, backing, cleanup, or exits | [Realization](realization.md) |
| Place interfaces, collect dependencies, construct syntax, or emit artifacts | [Artifacts](artifacts.md) |
| Implement text, formatting, printing, assertions, or reports | [Builtins](builtins.md) |

## Extending operations

Publish an operation's type, evaluation, and lifetime contracts in semantic
analysis. For operations using existing contracts, describe ordered inputs and
uses in `preparation/operands.cpp`, then realize syntax in
`realization/operation.cpp`. Uniform storage uses share the direct-child
definition in `semantic.semir.children`; mixed uses retain operand adapters.
Enumerate semantic expression alternatives explicitly in both visitors and
`CppOperation` alternatives in native realization. Structured control uses its
specialized realization paths.

New control scopes, ownership modes, or partial-object lifetimes require contracts
and checks at their owning boundaries. Aggregate changes must preserve initializer
sequencing and partial-object cleanup. Text and reports use ordinary preparation,
storage, and delivery contracts.

Validate generated behavior through native compilation and execution. Include
lifetime and observation cases when a change affects storage or sequencing.
