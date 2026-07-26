# Compiler architecture

This document describes the compiler stages, persistent representations, and
validation boundaries. Generated artifact and C++ lowering details belong to
[backend.md](backend.md).

Carven compiles one explicit, closed batch of `.cv` modules:

```text
CompilationRequest
  -> ParsedBatch
  -> SemanticConstruction
       -> structured HIR validation
       -> SolvedControl
       -> effect and availability diagnostics
       -> committed control facts
  -> SemanticProgram
  -> TargetGenerationPlan
  -> unit-local TargetUnit
  -> ArtifactSet
```

`ParsedBatch` and `SemanticProgram` are move-only owners with const queries.
Their IDs are local to the owning representation. `SemanticConstruction`,
analysis indexes, graphs, work queues, and verification state are transient.

## Stage API

```cpp
auto parse(const SourceManager&, std::span<const CompilationInput>) noexcept
    -> std::expected<ParsedBatch, Diagnostics>;

auto analyze(ParsedBatch) noexcept
    -> std::expected<Diagnosed<SemanticProgram>, Diagnostics>;

auto generate_target(const SemanticProgram&, TargetGenerationRequest) noexcept
    -> ArtifactSet;
```

Imports resolve only within the supplied input batch. The catalog rejects
duplicate canonical module paths and does not discover files.

## Parsing and declaration construction

Parsing validates input identity, module paths, source snapshots, syntax-tree
references, source spans, cross-tree references, and root node kinds. Token
buffers remain parsing state and are not published.

The catalog reserves `FunctionID`, `StructID`, `EnumID`, and `EnumCaseID`
before resolving declaration types. A `DeclarationSessionView` permits
recursive recipe solving while declarations are `Resolving`; successful
contracts become `Closed`, failures become `Failed`, and consuming the closed
set ends in `Finished`. Body elaboration reads closed contracts and does not
restart declaration resolution.

After bodies are built, unused-binding and unused-import diagnostics run while
syntax and catalog data are available. Syntax, module-analysis contexts, and
lint-only state are released before whole-program semantic solving.

## HIR ownership

`SemanticConstruction` owns canonical types, constants, declaration tables,
callables, interned failure sets, symbols, modules, expressions, statements,
patterns, blocks, lexical scopes, places, and bodies. Each exact closed failure
set has one `FailureSetID`; sorting by `HIRTypeID` is only the private
normalization used to intern unions and is not a semantic member order. Each
`HIRModule` contains only its ordered `items`.

`BodyID` identifies a body. Each `HIRCallable` and `HIRTestDecl` holds one
forward reference to its body. Function declarations refer to a callable;
closure expressions refer to a callable and do not embed or traverse its body
as part of the enclosing body.

After place derivation, structural validation starts from module items. It
checks that functions, tests, callables, and bodies have one owner; that every
body is complete; and that every expression, statement, pattern, and block
occurs exactly once in an acyclic structured body. A nested closure claims its
callable and schedules that callable's body for an independent traversal.
Scope parents must be construction-ordered, body root scopes are unique, a
closure root is a strict descendant of its enclosing scope, and each block,
arm, and loop scope resolves to the current body's nearest root.

Callable failure policy is stored once on `HIRCallable`:

```cpp
enum class HIRFailureContractKind {
    Inferred,
    Declared,
    UndeclaredPublished,
};
```

Declared and undeclared-published contracts remain fixed. An inferred contract
is solved from the callable body. An undeclared published callable that exposes
a failure produces the published-throw diagnostic.

## Control and failure solving

`solve_control` traverses structured HIR directly. The initial traversal
collects concrete callable call edges. Calls through callable signatures use
the signature's fixed failure contract. Call expressions do not retain a
derived target field; the solver derives a concrete callable, callable
signature, foreign, or error view from the callee type when needed.

Only inferred callables contribute outgoing dependency edges. A concrete edge
`u -> v` means that `u` depends on `v`; declared contracts are leaves whose body
dependencies do not enter failure inference. The solver also builds sorted
reverse-caller edges, then moves the forward graph into the shared SCC routine.

Within each component, a FIFO worklist starts with inferred members in
`CallableID` order. Evaluating one member reads the latest contracts and merges
its outward failures into a temporary normalized vector. Growth queues only
inferred callers in the same component. The finite, monotone failure sets
converge to the unique least fixed point without whole-program snapshots or
unchanged callable reevaluation.

After contracts stabilize, one recording traversal produces expression,
statement, block, catch, and unhandled-failure summaries. A separate structured
HIR traversal derives evaluation effects; it returns statement and block
effects transiently and retains only expression effects in `SolvedControl`:

```cpp
auto solve_control(SemanticConstruction&) noexcept -> SolvedControl;
auto commit_control_facts(SemanticConstruction&, SolvedControl&&) noexcept
    -> void;
```

Effect diagnostics query those summaries and perform focused HIR scans for
their own declarations and occurrences. Control evaluation, fixed-point
solving, evaluation-effect derivation, and fact publication are separate
implementation slices behind the same `SolvedControl` boundary. Control solving
may retain temporary failure vectors; publication interns every callable,
expression, block, catch, and try set and does not expose those vectors as
persistent facts.

## Availability

Availability runs after final failure contracts are known. A transient catalog
maps each place to its nearest body root and a dense body-local ID. Each body is
lowered backward from explicit continuations into a compact CFG whose normal,
transfer, call-failure, catch-guard, rethrow, and loop-backedge targets are
already resolved. Maximal linear chains are merged before solving.

The solver stores only block-entry states. Up to 64 local places use one inline
word; larger bodies use a contiguous word vector. A sparse witness list records
the structurally earliest Take site for each unavailable place. Joins OR the
bits and select canonical witnesses, so they are commutative, associative,
idempotent, and independent of successor order. A FIFO worklist reprocesses a
block only when its entry bits or witness changes. Diagnostics are emitted
after convergence, then the CFG, states, catalog views, and worklist are
released before the next body. No availability fact is retained in HIR. This
CFG is a disposable semantic-analysis projection; it does not prescribe
generated control flow. The target policy is defined by
[backend.md](backend.md#evaluation-and-control).

The semantic-analysis tail is:

```text
derive places
  -> validate structured HIR
  -> solve control and failure contracts
  -> diagnose effects
  -> diagnose availability body by body
  -> commit control facts
  -> validate type and nominal-storage contracts
  -> validate final facts and publish SemanticProgram
```

## Published semantic facts

`SemanticProgram` retains the facts required by diagnostics and lowering:

- resolved declarations, names, visibility, calls, types, constants, and
  pattern coverage;
- place roots and projections with Read, Write, ReadWrite, and Take access;
- `FailureSetID` references for callable contracts, expression pending,
  outward, and evaluation sets, block outward sets, catch acceptance, and try
  unhandled sets, plus test exits;
- canonical sorted `EvaluationEffect` place sets and control-boundary flags;
- nominal storage-dependency order for complete C++ definitions;
- source snapshots, module records, origins, spellings, and locations through
  `CompilationProvenance`.

Final verification checks table bounds, declaration and module ownership,
occurrence ownership, scopes, places and projections, callable shapes, failure
sets, effect place ranges and canonical order, patterns, nominal storage order,
visibility, and foreign boundaries. Each interned failure set is checked once
for normalized unique nominal members and unique set identity. Every published
failure reference is range-checked, and each expression's evaluation set must
equal the union of its pending and outward sets. Publishing moves the verified
tables into `SemanticProgram`.

## Shared graph ordering

Callable failure inference, nominal storage-cycle diagnosis, and backend
interface planning use the same SCC implementation. It consumes an owned
adjacency vector, normalizes it in place, and uses an explicit DFS-frame stack.
It validates edge ranges, sorts component members, returns components in
dependency-first topological order, and uses the smallest node ID to order
independent components.

## Stage boundaries

Persistent state is limited to facts required by a later stage. A stage may
make several focused traversals of structured HIR when that keeps its inputs
and outputs local. Parsing or analyzing opaque `#[cpp]` bytes, package
resolution, C++ compilation, linking, installation, optimization, and platform
selection remain outside semantic analysis.
