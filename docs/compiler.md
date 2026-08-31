# Compiler architecture

This document defines the implemented compiler pipeline, persistent
representations, ownership boundaries, publication gates, and dependency
direction. Observable language behavior and diagnostic identities belong to
[semantics.md](semantics.md). Generated-C++ realization belongs to
[backend.md](backend.md), and consumer/toolchain contracts belong to
[compatibility.md](compatibility.md).

Private helpers and module partitions may change. The lasting contract is that
each fact has one producer, owner, lifetime, verifier, and publication path.

## Pipeline

Carven compiles one explicit closed batch:

```text
CompilationRequest
  -> parse and close inputs
  -> ParsedBatch
  -> ProgramAnalyzer
       -> SemanticSession and SemanticDraft
       -> catalog and stable entity reservation
       -> declaration-contract resolution and freeze
       -> body elaboration and structural verification
       -> binding and place-use derivation
       -> failure/control/evaluation analysis
       -> effect diagnostics and flow-candidate freeze
       -> availability, contract, and nominal analysis
       -> exact-layout assembly and final verification
  -> SemanticProgram
  -> TargetProgram::build(move(SemanticProgram), TargetGenerationRequest)
  -> per-artifact TargetUnit
  -> GeneratedArtifact
  -> ArtifactSet
```

Parsing and semantic analysis return diagnostics without a partial
`SemanticProgram` on error. A successful `SemanticProgram` is move-only and is
consumed by target-program construction. The resulting `TargetProgram`
owns that semantic program by value for the complete target-generation
lifetime.

## Owners and lifetimes

| Owner or role | Owns | Lifetime |
| --- | --- | --- |
| `ParsedBatch` | closed syntax trees, source snapshots, module identities, and initial provenance | parse publication through semantic handoff |
| `ProgramAnalyzer` | syntax access, diagnostics, entry tracking, deferred callable constraints, and one semantic session | one semantic analysis |
| `SemanticSession` | provenance construction, one `SemanticDraft`, and the outer semantic publication gate | one semantic analysis |
| `SemanticDraft` | incomplete semantic slots, canonical builders and indexes, construction state, and compact fact candidates | construction through semantic seal |
| declaration capabilities | stable entity reservations and required declaration slots | declaration discovery through declaration freeze |
| recorded control workspace | rich failure, control, transfer, and diagnostic reasons | control analysis through flow freeze |
| availability workspace | one body's place catalog, CFG, states, witnesses, and worklist | one body only |
| `SemanticProgram` | exact verified structural, canonical, flow, binding, nominal, and provenance facts | semantic seal through target-program ownership |

Target-program ownership, artifact lowering, target units, and artifact
collection belong to [backend.md](backend.md).

`SemanticDraftStorage` and `SemanticProgramStorage` are distinct private
layouts. The draft layout may contain indexes, reservations, optional slots,
and construction vectors. The published layout can represent only final values.
Assembly moves final ID tables without remapping identities and leaves
construction-only state behind.

## Semantic stage gates

| Gate | Input | Required proof | Output |
| --- | --- | --- | --- |
| Input closure | request and source manager | normalized unique paths, unique canonical modules, valid snapshots, and closed imports | `ParsedBatch` |
| Declaration-contract freeze | catalog and reserved identities | every required named declaration slot and nominal capability is complete | immutable declaration view for body elaboration |
| Structural gate | completed declarations and bodies | valid IDs, scopes, bodies, ownership trees, type/form relations, bindings, and match-fact alignment | structurally verified `SemanticDraftView` |
| Flow-candidate freeze | solved failures and recorded control/evaluation | diagnostics complete; callable, expression, block, try, and effect columns total and normalized | immutable compact flow facts |
| Semantic seal | fully analyzed draft | exact published layout assembled; canonical tables, references, ownership, cycles, provenance, flow, binding, match, and nominal invariants verified | `SemanticProgram` |

An internal freeze makes already-decided facts immutable for the next analysis;
it does not publish another program. Only `SemanticSession::finish()`
constructs `SemanticProgram`, after `verify_semantic_program` accepts the exact
storage that will be returned.

## Input closure and identity

The parser consumes only paths supplied by the request. It validates source
identity, canonical module paths, snapshots, source spans, cross-tree
references, and root forms. File discovery outside the request belongs to the
caller or build integration.

Identity allocation follows the owning domain:

- source and module IDs enter semantic analysis through `ParsedBatch` and are
  never reallocated;
- origins and spellings share one append-only provenance domain across the
  frontend-to-semantic handoff;
- `SemanticEntityReservations` allocates stable symbol, function, structure,
  enumeration, and case identities directly for their final semantic tables;
- types, callable signatures, and failure sets are allocated only by their
  canonical value builders;
- availability and CFG identities are body-local and never enter
  `SemanticProgram`.

IDs are local to one compilation and identify values only in their owning
program.

## Declaration and body construction

The analysis catalog owns transient lookup and discovery data, not a second
copy of declaration facts. `DeclarationResolver` fills stable required slots
through `SemanticDeclarationCapabilities`; declaration freeze makes the
resolved contracts available to body elaboration without another publication
owner.

Closure callables are created during body elaboration, so structural
verification is the first gate that proves all named and closure callables,
bodies, scopes, and occurrence trees complete. Module items own top-level source
order, callables own their bodies, bodies and scopes own local occurrences, and
enumerations own their ordered cases.

Reverse indexes are allowed only as disposable projections for a measured
consumer. They are never a competing canonical owner.

## Published semantic facts

`SemanticProgram` is the only published semantic representation. Its storage
contains:

- provenance, source snapshots, modules, origins, spellings, and source
  locations;
- declarations, symbols, bodies, expressions, statements, patterns, blocks,
  scopes, and constants;
- canonical types, callable signatures, constant values, and failure sets;
- symbol-keyed binding facts;
- expression control facts and evaluation effects;
- expression place uses rooted directly in `SymbolID` with structural field or
  index projections;
- block control, try/catch, and callable flow facts;
- match-owned `HIRMatchCoverageFacts` aligned with source arms;
- nominal equality capabilities and direct by-value containment dependencies.

Place use does not create a second storage identity. The symbol is the root;
the projection path describes only how one occurrence reaches a subobject.
Types remain available from the canonical symbol/expression/type relations.

A callable's structural contract and effective flow are separate columns.
`CallContractView` is a pure projection of the verified callee type; call
expressions do not persist a competing resolved-target field. Concrete
callables use effective failure facts, first-class signatures use their fixed
contract, and foreign calls retain their explicit boundary classification.

## Failure, control, effects, and availability

Failure solving owns its dependency graph, SCC state, reverse callers, and
worklist. Rich `RecordedControlAnalysis` retains diagnostic reasons while effect
diagnostics run. The single consuming `freeze_flow_candidate` then publishes
only compact immutable callable, expression, block, try, and evaluation facts
and destroys the rich workspace.

Evaluation effects contain sorted unique read, write, and take symbol sets plus
the opaque reorder barrier. Whether an expression may terminate is a query over
its control fact, not a duplicated effect flag.

Availability starts only after compact flow and effect facts are immutable. It
builds and solves a disposable CFG for one body at a time, emits diagnostics
after convergence, and publishes no graph, state, witness, or availability ID.
Its CFG is an analysis projection and does not prescribe generated C++ control
shape.

Pattern coverage has one producer. The same coverage result diagnoses repeated
or covered alternatives and publishes arm-aligned `Reachable` or `Covered`
states plus exhaustiveness. Semantic control and availability still analyze the
source program; target construction and lowering consume the published arm
states instead of re-deriving reachability from pattern syntax.

## Nominal facts

Semantic analysis owns language-level nominal capabilities and direct by-value
containment. It verifies duplicate edges and cycles before publication. C++
complete-definition requirements, declaration order, interface components, and
artifact schedules are target facts and do not enter semantic storage.

## Provenance and diagnostics

The frontend establishes initial provenance, then the semantic session takes
exclusive ownership of the same append-only origin and spelling domains.
Existing identities never change. Diagnostics resolve stable source locations
while provenance is live; on success the finished provenance moves into
`SemanticProgram` for target attribution.

Diagnostic identities, severities, and required source locations are governed
by [semantics.md](semantics.md).

## Verification

Semantic verification is read-only proof, never a fallback producer. The
structural and final gates cover at least:

- ID bounds, table alignment, canonical uniqueness, and normalized ordering;
- exactly one owner for modules, declarations, bodies, expressions, statements,
  patterns, blocks, and scopes;
- valid callable/body, parent/scope, binding, type, and occurrence relations;
- acyclic expression, statement, body, scope, and nominal containment graphs;
- total and aligned control, effect, place-use, try, block, callable-flow, and
  match-coverage columns;
- normalized failure sets and valid callable/call projections;
- valid origins, spellings, source IDs, and module IDs.

A verifier may recompute a relation to check an invariant. It does not publish
the recomputed relation or repair invalid storage.

## Dependency direction

- frontend depends on source, diagnostics, and support vocabulary, not semantic
  or backend modules;
- semantic construction depends on frontend input, source/provenance,
  diagnostics, shared support, and semantic vocabulary;
- `SemanticProgram` and semantic vocabulary do not depend on target or backend
  modules;
- target-program construction consumes a move-only verified `SemanticProgram`;
- the lowering entry receives the target program and an artifact identity;
  artifact-local lowerers receive only the resulting focused view,
  target-building vocabulary, and immutable semantic vocabulary exposed through
  that view;
- target units, rendering, and artifact collection do not depend on semantic
  construction state.

Build and test evidence belongs to [testing.md](testing.md); source conventions
belong to [conventions.md](conventions.md).
