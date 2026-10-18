# Compiler architecture

This overview defines pipeline ownership, publication order, dependency direction,
and the handoff from source analysis to C++ generation.

## Subsystems

| Reference | Scope |
| --- | --- |
| [Construction](analysis/construction.md) | Declaration and expression completion, solving, and native delegation |
| [Semantic representation](analysis/semir.md) | Identity, canonical facts, structured operations, and published contracts |
| [Ownership analysis](analysis/ownership.md) | Availability, storage loans, call relationships, and pointer nullability |
| [Semantic execution](analysis/evaluation.md) | Execution interfaces, static roots, storage, freezing, output, and interpretation |
| [C++ generation](backend/README.md) | Planning, representation selection, realization, and artifact emission |

## Pipeline

```text
SourceBatch → SyntaxProgram → ProgramDraft → SemIRProgram
```

`source.batch` defines source identities and canonical module paths through
`SourceBatch` and `SourceModuleInput`. In `compiler.analysis`,
`analyze_compilation` sequences parsing and semantic analysis. Its
`CompilationAnalysis` owns source
snapshots, diagnostics, requested source observations and captured output, and a
published program on success. Failed analysis retains diagnostic sources.
Output capture and source observations are independent options; output can be
forwarded synchronously without retaining a copy.

`compiler.compile` analyzes sources, then calls `generate_artifacts` with the
published program. The driver loads or analyzes inputs. Check optionally retains
semantics through `-o`; compile generates C++. Native execution analyzes and
generates in memory. Interpret executes the published program; dump consumes
lexical or syntax results directly. Errors prevent program delivery; warnings
accompany successful results.

Standard-library analysis also retains `CheckedDefinitions`: completed
construction rows containing generic and staged bodies for consumer
instantiation. Both definitions and the executable program are delivered only
after successful semantic publication. Ordinary and test C++ generation share
that program without repeating analysis or static execution.

`compiler.artifact` packages the program, optional checked definitions, module
ownership, and cacheability. Source identities remain in semantic provenance.
An artifact-directory input alone supplies the program to the backend; standard
library artifacts accompanying new sources supply definitions to construction
and specialization. `compiler.manifest` projects inputs, artifact inventory,
owned runtime-test count, and cacheability for build integration.

## Source and command boundaries

`driver/` owns options, file loading, diagnostic presentation, artifact output,
and native process execution. `driver.cli` normalizes compiler process arguments
through `support.process` before command dispatch. The formatter uses the same
adapter. Both entries retain UTF-8 argument storage while command parsers borrow
string ranges.
`support.process` also reads UTF-8 environment values; `driver.run` selects
`CXX` and applies the default compiler name.
`driver.sources` collects `.cv` and `.cpp` files from the toolchain and
working-directory Crafts roots, deduplicates canonical paths, and sorts by
absolute physical path. Checking receives physical paths and resolved module
identities; imports resolve within this closed batch.
Mapped inputs select the build system's owned source batch. Dependencies come
from retained standard-library definitions or matching toolchain sources.

`load_and_analyze_sources` prepares the batch and invokes analysis.
`diagnostics.report` renders diagnostic codes and source spans; the driver owns
terminal styling and transport. `driver.process` owns process launching,
executable lookup, and temporary run directories. Project build integration owns
source selection, cache validity, artifact publication, native scheduling, and
linking. Module implementations report their provenance source; synthetic
artifacts have no source owner.

`source.document` shares immutable source text and line indexes.
`frontend.document` retains syntax and diagnostics in `ParsedDocument`.
`frontend.ast.tree` shares immutable tree storage while binding source identity
per compilation; `frontend.ast.storage` owns nodes and read-only views, and
`frontend.ast.topology` supplies structural traversal.

`parse_recovering` retains complete top-level items around recoverable syntax
errors for source queries. Lexical errors, delimiter preflight failures, and
invalid initial imports provide no tree. Ordinary parsing rejects results with
diagnostics. `parse_program` reuses retained documents or parses missing inputs,
validates source bindings, rebinds diagnostics, and constructs fresh provenance
and resolved imports. Recovered syntax does not enter compilation.

## Source queries

`semantic.analysis.source` delivers optional `SourceAnalysisOutput` batches with
selected entities, alias binding origins, and types at source tokens. Aliases
navigate to bindings; source entities navigate to declarations. References retain
local spellings; provided exports supply module metadata. Successful publication
supplies program-owned types. Failed analysis can retain definitions and known
builtin types from admitted declarations and completed bodies. These observations
do not establish program validity. Recipients copy records they retain; empty
recipients perform no recording.

`tools/workspace` owns document revisions, snapshots, selections, navigation
indexes, and bounded caches of complete analysis results. Bulk document updates
validate and publish together. Navigation selects transitive source dependencies,
including standard-source default and associated entries; semantic and reference
queries use the explicit project selection. Each uncached selection reuses
parsing and runs the complete semantic transaction and publication checks.
Workspace analysis captures static output and diagnoses reached host input,
clock sampling, or waits. Snapshots retain revisions and query results across
later edits. CLI and workspace share source, parsing, and analysis owners;
LSP transport and Xmake targets remain outside the workspace API.

## Subsystem ownership

| Owner | Responsibility |
| --- | --- |
| `frontend/` | Syntax, literals, parsing, and the closed syntax program |
| `semantic/analysis/` | Contextual typing, declaration and body completion, admission, diagnostics, and validation |
| `semantic/evaluation/` | Typed execution values, constant operations, bounded execution, and freezing |
| `semantic/semir/` | Semantic operations, canonical values, identity, and publication contracts |
| `semantic/format/` | Format syntax, serialization, and bounded builtin formatting |
| `backend/preparation/` | Borrowed body facts, operand demands, operation plans, and encoding/size proofs |
| `backend/` | Representation selection, C++ realization, and emission |
| `crafts/carven/runtime/` | Native support for language operations |
| `crafts/carven/std/` | Standard APIs, algorithms, containers, and their native support |

Construction requests static execution when results determine declaration types
or array extents. The executor consumes typed semantic operations and an
execution context; the analysis adapter supplies completion and diagnostics.
Evaluation interfaces contain no analysis, AST, or target syntax types.
Compiler support supplies host input and clock access. Execution owns host
values, budgets, readiness, cancellation, timers, and structured report data.
Generated code uses runtime adapters for the corresponding native operations.

Semantic analysis establishes source types, coverage, evaluation order,
ownership, and failure contracts. Preparation derives implementation choices
from published facts without changing semantic stores. Realization preserves
effects, storage observations, lifetimes, and cleanup; emission serializes target
syntax. C++ checks delegated native operations and supplies optimization and
machine code. Unknown optional facts retain the ordinary operation.

## Ownership and identity

`SyntaxProgram` owns provenance and resolved imports while sharing immutable
syntax and source storage. `ProgramDraft` consumes it and owns declarations,
canonical interning, construction types, failure constraints, and body
construction. Declarations may reserve identities for recursion and forward
references. Each body has one owning callable.

Solving consumes construction state. `SemIRProgram` owns resolved semantic data
and provenance; downstream consumers use no source syntax. Program, provenance,
plan, and table owners support required move construction, not replacement by
move assignment. End borrows before moving or consuming owners. Construction
readers return copies where storage may grow; final views borrow immutable
storage. Query boundaries check identity and bounds; construction boundaries
check unique definitions and structure.

Callback ports use `FunctionRef<R(Args...) noexcept>` from
`support.function_ref`. It copies function pointers and borrows lvalue callables.
Borrowed objects must outlive every invocation, including after coroutine
suspension. Empty views represent optional recipients and must not be invoked.
Escaping exceptions terminate at the callback boundary.

Within the pipeline, `draft` denotes `ProgramDraft`, `semantic` denotes published
`SemIRProgram`, and `compilation` denotes `PlannedCompilation`, which owns a target
plan and borrows the program. Keep source-module and semantic-module identities,
failure terms and solved sets, and control-flow completion distinct.

## Publication gates

`ProgramDraft::finish()` in `analysis.program` consumes the draft through these gates:

1. Require completed declaration and body reservations, including nominal
   instances and callable results. Catalog and import-use state end before solving.
2. Solve failures, test-stop effects, and types; finalize signatures,
   declarations, bindings, patterns, and bodies. Every operation and region,
   including inactive source, must contain resolved facts.
3. Construct the local `SemIRProgram`, validate storage topology, compute type
   contents, and check body and global contracts.
4. Use one `AnalysisModelView` over concrete bodies and the normalized generic
   snapshot for async admission, ownership, and pointer nullability, in that
   order. Ownership publishes Read passing facts. End these borrows before
   releasing checked source bodies.
5. Record callable/type surfaces, generic dependencies, and closure reference
   order. Release checked regions and static-only bodies. Surviving bodies keep
   their IDs and one executable region; source templates and static tests retain
   declarations and provenance without body references.
6. Solve cancellation capability over finalized callable and operation-value
   dependencies to a least fixed point. Unknown producers remain possibly
   cancelled. Publish callable and await-completion facts with the program.

Standard-library analysis retains completed construction and catalog rows in a
separate owner before consumption, then delivers them only after these gates pass.
Backend preparation consumes published facts; realization checks exit receivers,
and native compilation checks the completed C++ representation.

## Craft boundary

Standard entries combine compiler-provided declarations and ordinary source
exports under one module identity. Standard and user modules share discovery,
resolution, completion, analysis, and publication. Default and type-associated
entries can select source functions with their own bodies and signatures.

`Operation`, numeric and SIMD operations, and heterogeneous printing use
compiler-provided contracts. Types such as `Read<T>`, `Duration`, `RangeError`,
`Timeout`, parse and UTF errors, input handles, and time points are standard-source
declarations. Compiler-recognized operations obtain those declarations through
ordinary catalog lookup and completion. Checked text construction, input error
mapping, and public waits are source bodies; host input, clock sampling, and
timer waits use module-local provided operations.

The compiler build has no dependency on Crafts headers or native libraries.
Generated programs use native support owned by each capability, including
networking socket resources. Source wrappers obey ordinary type, access,
ownership, failure, and execution rules in both semantic and generated execution.
Native imports retain their delegated contracts.

## Diagnostics and dependencies

Produce diagnostics at the rule owner. Source operations retain origins;
implicit operations use linked expansion origins. Body construction continues
past failed functions, tests, and `const` blocks. Record a failed function once;
later requests fail without repeating its diagnostic. Stages requiring completed
bodies run only after construction reports no error. Compiler-private invariant
failures terminate at the violated boundary.

Frontend facilities depend on source and syntax. Construction consumes syntax;
SemIR owns data, storage, and structural contracts independently of AST and
analysis. Post-solve analysis and the backend consume finalized semantics.
Runtime support implements native operations; build orchestration supplies
source batches and native build inputs.

## Timing

The driver owns optional accumulation, totals, and reporting after resource
cleanup. `support.timing` supplies synchronous interval recipients and monotonic
clock scopes. Lexing, parsing, analysis, and generation measure their own work;
the driver measures filesystem and process stages. Empty recipients do not read
the clock.

Lexing and parsing accumulate across modules. Semantic timing includes catalog,
declarations, bodies, solving, validation, source observations, and requested
static execution. Detail intervals are included in the total; do not add them
again. Workspace loading and occurrence indexing have separate intervals. Cache
hits emit no compiler events. End-to-end timing also includes input preparation,
import selection, cache maintenance, and owner teardown.
