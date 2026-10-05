# Compiler architecture

This document describes the compiler pipeline, including semantic construction
and analysis, publication gates, ownership boundaries, dependency direction, and
the handoff of an immutable semantic program to C++ generation.

## Reading guide

The overview defines phase ownership, publication order, and dependency direction.
Use the following references for changes within a subsystem:

| Reference | Scope |
| --- | --- |
| [Construction](analysis/construction.md) | Declaration and expression completion, solving, and native delegation |
| [Semantic representation](analysis/semir.md) | Identity, canonical facts, structured operations, and published contracts |
| [Ownership analysis](analysis/ownership.md) | Availability, storage loans, call relationships, and pointer nullability |
| [Semantic execution](analysis/evaluation.md) | Execution interfaces, static roots, storage, freezing, output, and interpretation |
| [C++ generation](backend/README.md) | Planning, representation selection, realization, and artifact emission |

[Language reference](../language/README.md) defines source validity and observable
behavior.

## Pipeline

`source.batch` defines `SourceBatch` and its `SourceModuleInput` records using
source identities and canonical module paths.
`compiler.analysis` sequences parsing and semantic analysis through
`analyze_compilation`, returning a published program and structured diagnostics.
`compiler.compile` calls this entry and then `generate_artifacts` to consume the
published program and produce C++ artifacts. CLI compile and native-run commands
use the same stages. Execution output is delivered synchronously to the supplied
recipient.

`driver/` owns command options, file loading, diagnostic presentation, artifact
output, and native process execution. `driver.process` owns POSIX/Windows
process launching, executable lookup, and temporary run-directory creation.
`driver.sources` owns executable-relative Crafts lookup and collection from
the toolchain and working-directory Crafts roots. It supplies physical source paths
and resolved module identities to checking, C++ generation, and execution.
It recursively collects all `.cv` and `.cpp` files in those roots, deduplicates
canonical filesystem paths, and sorts inputs by path spelling. Module imports
resolve within the collected Carven batch. The direct-run driver compiles collected
native sources together with generated implementations and executes the result.
`load_and_analyze_sources` prepares the batch, calls `analyze_compilation`, and
renders diagnostics using the source manager's line index for byte locations and
line ranges.
The driver selects terminal styling and writes diagnostics to standard error.
It presents command failures with usage hints for invalid invocations.
`diagnostics.report` renders source diagnostics from their codes and source spans.
Compile and run commands send the program to the backend; interpret sends it to
the interpreter. Check completes after successful analysis without invoking a
backend or interpreter. Dump commands consume lexical or syntax results directly.

The driver owns optional timing accumulation, command totals, and reporting.
`support.timing` supplies a synchronous interval recipient and monotonic-clock
scopes. The `lex`, `parse`, `analyze`, and `generate_artifacts` entries measure
their own execution; composition forwards the recipient. The driver measures
filesystem and process stages. Empty recipients do not read the clock. Reporting
follows command-resource cleanup.

```text
SourceBatch → SyntaxProgram → ProgramDraft → SemIRProgram
```

`frontend.ast.tree` owns the syntax tree and its root. `frontend.ast.storage`
provides node storage and read-only views. `frontend.ast.topology` supplies
structural traversal for syntax construction and validation.

`parse_recovering` exposes complete top-level syntax items retained after parser
recovery, together with error diagnostics. Delimiter preflight and initial import
failures provide no tree. The `parse` entry remains strict and rejects these
recovered results when diagnostics are present; recovery does not admit incomplete
source into compilation. Editor queries can consume retained declarations while
keeping the source and tree owners alive.

`parse_program` parses the closed source batch and resolves module imports.
`analyze` constructs declarations and typed structured bodies, solves types and
failure sets, validates contracts, checks ownership and callable loans, and
publishes an immutable semantic program. Errors prevent delivery; warnings accompany
a successful result.

`semantic.analysis.source` defines `SourceOccurrence` and its optional synchronous
recipient, `SourceAnalysisOutput`. Analysis records source occurrences at identity
resolution sites. Declaration and nominal gates control delivery of declaration
names; each successfully constructed body contributes its own observations.
Type occurrences use direct AST token spans rather than enclosing expression
ranges. Locations and definitions can survive an unrelated body error. After successful
publication, types use `TypeID` values owned by the same semantic program. Failed
analysis retains only known `BuiltinType` values. Observation records contain no
draft identities or borrows, and do not establish solved failure or ownership
contracts. An empty recipient performs no recording. The analysis entry delivers
one observation batch at completion; publication gates remain unchanged.

`tools/editor` owns document revisions, retained snapshots, lazy source queries,
and content caching. Its provider analyzes an explicit closed module set in full
when selected content changes. See [Editor analysis](../../tools/editor/README.md)
for ownership, query contracts, invalidation, and measurement commands.

## Design considerations

Carven establishes source types, coverage, evaluation order, ownership, and
failure contracts. Static roots, `const` blocks, static tests, and
interpretation execute shared semantic operations with separate admission and
completion rules.
Only explicit `const fn` bodies can be called in required contexts. After
ordinary body construction, each `const fn` definition is checked for executor
capability on its semantically reachable paths and for marked direct callees.
The interpreter checks operation support on paths it executes. Execution and
constant-result publication retain their own checks.

The backend specializes operations using established semantic facts while
preserving effects, storage observations, and lifetimes. C++ supplies native type
properties, template instantiation, optimization, and machine code. Runtime
support uses standard-library numerical conversion.

Semantic facts retain the identity and scope of the operation or storage they
describe. Ownership, nullability, normal-completion constants, and slice extents
have distinct propagation rules.
Optional analyses define their fact domains, invalidation rules, and stopping
conditions; unknown facts retain the ordinary operation.

Backend preparation derives implementation plans from published facts without
modifying semantic stores. Realization preserves execution, storage, and cleanup
obligations while delivering the selected result; emission serializes target
syntax. Semantic analysis owns source facts, preparation owns implementation
selection, and runtime performs the remaining work.

## Subsystem ownership

Directories group semantic responsibilities. Construction requests static
execution when its results determine declaration types or array extents.

| Owner | Responsibility |
| --- | --- |
| `frontend/` | Source syntax, literals, parsing, and the closed syntax program |
| `semantic/analysis/` | Contextual typing, declaration and body completion, source admission, diagnostics, and validation |
| `semantic/evaluation/` | Typed execution values, constant operations, bounded structured execution, and freezing |
| `semantic/semir/` | Semantic operations, structured format data, canonical values, identity, and publication contracts |
| `semantic/format/` | Source format syntax, serialization, and bounded builtin formatting of observed values |
| `backend/preparation/` | Borrowed body facts, operand demands, operation plans, and encoding/size proofs |
| `backend/` | Representation selection and C++ realization from published semantic facts |
| `crafts/carven/runtime/` | Shared native support for language operations under their semantic contracts |
| `crafts/carven/std/` | Standard-library APIs, algorithms, containers, and their native implementation support |

Semantic execution owns host values, bounded display, and structured report
data. Runtime components own their native implementations. Compiler source does
not include runtime headers; backend symbol metadata describes the runtime
dependencies of generated C++. Cross-mode tests check shared observable contracts.

The structured executor consumes typed semantic operations and an execution context.
The analysis adapter supplies declaration completion and diagnostics. Evaluation
depends on semantic representations; its interfaces contain no analysis, AST, or
target syntax types. Backend preparation consumes published constants and operation
facts.

## Ownership and identity

`SyntaxProgram` owns source provenance, syntax trees, and resolved imports.
`ProgramDraft` consumes it and owns mutable declarations, canonical interning,
construction types, failure constraints, and body construction. Canonical type
interning compares complete values before reusing an ID. Published types retain
their insertion order. Declarations may reserve identities for recursion and
forward references. Each callable body has one owning callable; the immutable
declaration store retains that relation for lookup.

Solving consumes construction state. `SemIRProgram` retains resolved
semantic data and provenance; lowering does not query source syntax. Program,
provenance, plan, and table owners permit move construction where required, but
not replacement through move assignment. Borrows end before an owner moves or
is consumed. Construction readers return copies because storage may grow;
final views borrow immutable storage. Query boundaries check identity and bounds;
construction boundaries check unique definitions and structure.

Within the pipeline, `draft` names mutable `ProgramDraft` state, `semantic` names
published `SemIRProgram` data, and `compilation` names a `PlannedCompilation`
owner. Source-module and semantic-module identities remain distinct. Failure
terms, solved failure sets, and control-flow completion name separate facts.

## Publication gates

Construction completes declarations and bodies before publication. The gates
establish the resolved facts and query contracts consumed by later stages.

`ProgramDraft` owns pending function heads. A complete callable contract includes
its result. Declaration-head completion closes the nominal tables; solving requires
complete callable contracts and bodies. The catalog and import-use state end before
solving. Final semantic validation checks declaration surfaces, including closure
captures and solved failure sets.

`analysis.program` owns `ProgramDraft` and declaration/body reservations.
Its consuming `finish()` checks reservation completeness, solves failures and
types, and finalizes callable signatures, declarations, and bodies in that order.
Test-stop effects are solved before body completion and publication.
Body completion resolves type and failure facts in the owned operation tree and
finalizes binding and pattern tables. Identity array adoptions are removed while
preserving the source storage and the consumer's access.
The `SemIRBody` boundary requires resolved facts in every operation and region,
including inactive source.

`finish()` constructs a local `SemIRProgram`. Its constructor validates storage
facts and topology, then computes type contents. After releasing the consumed
draft, syntax, imports, and construction solutions, `finish()` checks body
contracts, global semantic contracts, ownership, and local pointer nullability,
in that order. All checks read `const SemIRProgram&`.

Before returning the program, publication records callable/type surfaces and
closure construction order, then releases checked source regions and static-only
bodies. Surviving bodies keep their IDs and contain one executable region.
Source-template callables and completed static tests retain declarations and
provenance without a body reference. No source tree is available to downstream
planning or execution.
Source diagnostics use a separate channel. Body contracts establish the parameter
and binding relations used by global checks. Successful checks deliver the program.

Backend preparation derives operand uses and execution summaries from published
operations. Realization preserves their lifetime and control contracts and checks
that all emitted exits have receivers. Target verification checks the completed
C++ representation.

## Craft implementation boundary

Standard crafts express `.cv` implementations through Carven declarations and
operations. Lowering supplies the native dependencies of language operations.
Runtime header imports and runtime implementation names are reserved for C++
support source.

A craft owns its library API and may include C++ implementation support alongside
its source modules. That support may include runtime headers. Runtime provides
shared language facilities and does not depend on standard crafts.

Standard and user crafts use the same type, ownership, static-execution, and
publication rules. Library identity comes from resolved declarations. Native C++
imports retain their delegated contracts.

## Diagnostics and dependencies

Source operations retain origins. Implicit operations use linked expansion
origins. Diagnostics are produced at the rule owner. Body construction continues
past a failed function, test, or `const` block. A failed function is recorded
once; a later request for its body or inferred contract fails without another
diagnostic. Stages that read completed bodies run only after construction
reports no error. Compiler-private invariant failures terminate at the violated boundary.

Frontend facilities depend on source and syntax. Semantic construction consumes
syntax; SemIR owns data, storage, and structural contracts without depending on
AST or analysis. Post-solve analysis consumes final operations. The backend
consumes published semantics. Runtime support implements native operations;
build orchestration supplies source batches and native build inputs.
