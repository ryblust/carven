# C++ backend architecture

This document defines the implemented semantic-to-C++20 realization pipeline,
target-program ownership, unit invariants, and emission boundary. Observable
Carven behavior belongs to [semantics.md](semantics.md). Generated-C++ consumer
modes, artifact roles and paths, numeric-model requirements, and installed
headers belong to [compatibility.md](compatibility.md).

Private C++ spelling, builder layout, and traversal order may change within
those contracts. Target decisions must still have one owner and one verified
publication path.

## Pipeline and owners

```text
SemanticProgram + TargetGenerationRequest
  -> TargetProgram::build(move(SemanticProgram), move(request))
       -> names and module identities
       -> type recipes and target-call signatures
       -> failure profiles, carrier shapes, and conversion matrix
       -> binding representation requirements
       -> typed interface/artifact graph and complete schedules
       -> target-program verification and seal
  -> TargetProgram
  -> TargetArtifactView for one TargetArtifactID
  -> TargetUnitLoweringContext
  -> TargetUnitBuilder::finish(TargetUnitRoot)
  -> TargetUnit
  -> emit(TargetUnit)
       -> TargetRenderer
       -> GeneratedArtifact
  -> ArtifactSet
```

`TargetProgram` is move-only and owns the sealed `SemanticProgram` by
value. It is the only lifetime root and decision surface for lowering.
`lower_target_unit` receives the target program and an artifact identity,
creates a focused `TargetArtifactView`, and returns a self-contained
`TargetUnit`.

| Owner or role | Owns | Lifetime |
| --- | --- | --- |
| `TargetProgramBuilder` | construction indexes, target interners, name allocation, graph workspaces, and schedules | one target-program build |
| `TargetProgram` | semantic source and every sealed target-wide fact | all artifact lowering for one generation |
| `TargetArtifactView` | one borrowed focused query surface over semantic records and target projections | one artifact lowering transaction |
| `TargetUnitLoweringContext` | focused view, unit builder, local type materializations, and local caches | one artifact |
| callable/module lowerers | local names, scopes, sequencing, and control destinations | one lowering scope |
| `TargetUnitBuilder` | unit-local type, expression, statement, and item arenas | unit construction through seal |
| `TargetUnit` | verified target graph, root metadata, directives, and sections | unit seal through rendering |
| `TargetRenderer` | layout and physical/logical source-position state | one serialization |
| `GeneratedArtifact` | logical path, role, mapping policy, and rendered content | emission through collection |
| `ArtifactSet` | unique canonically ordered artifacts | compilation result |

## TargetProgram construction and seal

The private `TargetProgramBuilder` performs four ownership-complete operations:

1. allocate stable module, namespace, entity, payload-enum, and reserved source
   names;
2. derive total target type recipes, target-call signatures, failure profiles,
   carrier shapes, conversion classifications, and value-binding requirements;
3. collect only target-relevant declaration and implementation references and
   build the typed interface/artifact graph;
4. finalize logical paths, directive groups, source-mapping policy, artifact
   roles, and complete declaration schedules.

Target-program verification then proves total coverage of semantic key domains,
canonical target identities, valid references, carrier-conversion laws,
artifact metadata and path safety, role/schedule agreement, and deterministic
dependency-first schedules. Construction graphs, indexes, and mutable name
allocation state do not survive the seal.

After seal, canonical target identities cannot be created. Unit-local caches
may memoize materialization of an existing recipe, but they cannot choose a new
representation or become another authority.

## Focused lowering surface

`lower_target_unit` receives `TargetProgram` only to select one artifact and
construct its `TargetArtifactView`. Within `TargetUnitLoweringContext`, that view
is the only source query surface. It combines immutable semantic records with
their sealed target projections and provides focused access to schedules,
names, type recipes, target-call signatures, failure profiles, carrier
conversions, binding requirements, provenance, and the semantic nodes needed
for that artifact.

Callable and module lowerers receive the focused view and follow its typed
artifact schedule. Names, representations, dependencies, order, and paths are
sealed target-program facts.

One `TargetUnitLoweringContext` owns one fresh unit builder. Target type recipes
are materialized as unit-local `TargetType` nodes and cached by semantic type and
active module. Qualification is a contextual projection of a sealed recipe,
not a second representation decision.

## Realization responsibility

Semantic analysis supplies closed source-language facts. The backend owns their
source-to-C++20 realization: generated types and signatures, value category,
sequencing, private failure transport, declaration and interface schedules,
artifacts, and source attribution.

Supported consumer modes and toolchain responsibilities are defined by
[compatibility.md](compatibility.md#compiler-and-toolchain-boundary). The
`#[cpp]` boundary and stable diagnostic identities are defined by
[semantics.md](semantics.md).

## Types, target signatures, and names

Every semantic type has one target recipe. Recipes distinguish intrinsic and
nominal identities, arrays, callable and function-reference signatures,
deduced/foreign forms, parameter passing policy, and the traits required by
lowering.

`TargetArrayExtent` is a type-owned value. An array type can therefore be shared
without treating its extent as an expression occurrence. Array construction
expressions retain their own occurrence-owned extent expression when the
generated syntax requires one.

Every concrete callable and first-class callable signature maps to one
interned `TargetCallSignatureRecipe`. Equal target signatures may share
identity. Parameter passing is decided once and referenced by every declaration,
call, callable view, and type materialization.

A target-call signature is a compiler-private identity for generated C++
parameter, result, value-category, and carrier forms; it is not a platform ABI.

Stable target names are allocated once by the target program. Relative and
qualified spellings are context projections of the same identity. Unit-local
allocation is limited to bindings, temporaries, and synthetic labels.

## Failure profiles and carrier transport

Semantic failure sets own normalized membership but no target order.
Target-program construction maps each semantic set to one deterministic
`TargetFailureProfile` and each target-reachable result/profile pair to one
canonical `TargetCarrierShape`.

Non-failing signatures use direct results. Failing target-call signatures,
declarations, calls, propagation, try lowering, and continuations all reference
the same carrier authority. One sealed conversion matrix classifies equal
profiles as identity and strict covered expansions as widening; narrowing,
incomparable profiles, and result changes are invariant violations.

Lowering applies the sealed classifier to an actual occurrence. It does not
recompute failure order, set inclusion, or carrier necessity in separate syntax
domains. Runtime `Outcome` and `FunctionRef` remain private generated-code
support; their representation and platform ABI are not a handwritten-C++
compatibility promise.

## Interface and artifact graph

Semantic analysis supplies declarations, visibility, nominal containment, and
references. Target-program construction applies C++ representation and
completeness rules:

- declaration-only requirements may use deterministic forward declarations;
- published complete-definition requirements form interface edges;
- body-only and call-graph cycles do not merge interfaces;
- a true complete-definition cycle forms one interface SCC artifact.

Each `TargetArtifactSpec` has one typed identity and contains its logical path,
role, source-mapping policy, directive groups, typed dependencies, and complete
schedule. Interface schedules own component members, forward declarations, and
declarations. Module schedules own nominal implementation order, opaque
preamble items, private declarations, definitions, tests, and entry placement.

The component anchor and public logical-path rules are governed by
[compatibility.md](compatibility.md#generated-artifacts). Unit lowering resolves
typed artifact directives to self-contained unit directives; it does not derive
paths or cross-artifact order again.

## Evaluation and control

Semantic control facts provide failures and transfer behavior; evaluation
effects provide sorted read, write, and take sets plus the opaque reorder
barrier. The sequencer materializes a value only when required by
non-commuting access, ownership transfer, C++ value category or lifetime,
failure transport, or an opaque boundary.

Lowering prefers direct structured C++ conditionals, loops, returns, matches,
and regions. A narrow synthetic route is used only where C++ has no direct
structured form and the route preserves initialization and scope-entry rules.
Synthetic control remains unit-local and is checked by the unit verifier.

### Match pruning

Semantic analysis publishes arm-aligned `HIRMatchCoverageFacts`. Target-program
reference collection and match lowering consume those facts and omit `Covered`
arms, including their target-only dependencies, while retaining the semantic
warning produced by the coverage owner.

Pruning does not change subject evaluation, sequencing, value category,
materialization, or lifetime. The subject is evaluated exactly once, and each
repeated target syntax use owns an independent expression occurrence.

### Typed C-style `for`

C-style loop lowering classifies initializer and step forms before inserting
their occurrences into target arenas. Legal header forms enter
`TargetForInitializer` and `TargetForStep` directly; other forms are emitted
exactly once by the structured fallback. No wrapper statement is allocated for
syntax owned by the header.

If a condition or step needs preludes, lowering uses a structured `while`
normalization and a narrow `ForLoopContinue` jump role when required.
`TargetForStmt` owns typed initializer and step values plus occurrence-owned
condition and body references.

## Optimization boundary

The backend performs correctness-required sequencing and materialization plus
target normalization justified by verified semantic facts. General optimization
belongs to the downstream C++ toolchain, and generated-program correctness does
not depend on optimizer transformations.

Covered-match-arm pruning is one such normalization. It may change private
generated bytes and target-only dependencies while preserving diagnostics and
source behavior.

## TargetUnit and verification

`TargetUnit` owns one `TargetStorage` containing type, expression, statement,
and item arenas plus one `TargetUnitRoot`. The root contains the logical path,
artifact role, source-mapping policy, resolved directive groups, and preamble,
body, and epilogue sections.

Types are interned values and may be shared. Items, statements, and expressions
are syntax occurrences and have exactly one structural owner. Reuse requiring
multiple occurrences goes through `clone_expression_occurrence`, which deep
clones expression and nested statement occurrences while continuing to share
types.

`TargetUnitBuilder::finish(TargetUnitRoot)` is the only completion operation. It
consumes the builder, constructs the complete unit, runs the owner-local
validation seam, and converts any violation into an invariant failure before
returning the unit. The validator checks at least:

- all typed references and type/value references are in range;
- root sections reach every item, statement, and expression occurrence exactly
  once;
- occurrence graphs are acyclic and types obey their value invariants;
- source-owned nodes have origins and compiler-owned nodes have explicit
  synthetic attribution;
- artifact role, source-mapping policy, directives, and root shape agree;
- typed `for` headers contain only their admitted forms;
- lowered jump roles agree with labels, loops, and initialization barriers;
- no arena node is orphaned.

The pure validation function is an owner-local test seam, not a recoverable
product error channel. Its scope is the target graph and explicitly modeled
synthetic-control rules. C++ and `#[cpp]` validation occur at the boundaries
defined by [compatibility.md](compatibility.md#compiler-and-toolchain-boundary)
and [semantics.md](semantics.md#cpp-boundary).

## Rendering and artifact collection

`TargetRenderer` receives only a complete `TargetUnit`. It exhaustively
serializes directives and nodes, applies the explicit source-mapping policy,
and chooses whitespace, indentation, and line breaking. It does not inspect
semantic or target-program state, infer artifact role, choose includes, derive
a path, or repair missing target decisions.

`emit` transfers unit metadata and rendered content into one
`GeneratedArtifact`. `ArtifactSet` validates normalized relative paths,
uniqueness, prefix safety, and canonical ordering. Filesystem mutation,
dependency scanning, C++ compilation, linking, installation, and platform
selection occur downstream.

## Dependency direction

- target-program construction depends on immutable semantic capabilities and
  shared target vocabulary;
- lowering depends on focused `TargetArtifactView`, unit-building vocabulary,
  and semantic vocabulary exposed through that view;
- unit storage and validation do not depend on semantic construction modules;
- rendering depends only on complete target-unit and layout vocabulary;
- artifact collection does not depend on semantic or target-program
  construction;
- generated runtime support does not depend on compiler internals.

Build and test evidence belongs to [testing.md](testing.md); source conventions
belong to [conventions.md](conventions.md).
