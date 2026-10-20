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
semantics through `-o`; compile generates C++. Interpret executes the published
program; dump consumes lexical or syntax results directly. Build and run execute
Standard Craft build scripts, which call the compiler in process and schedule
native tools. Errors prevent program delivery; warnings accompany successful results.

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
implementation dependencies, owned runtime-test count, and cacheability for build integration.

## Source and command boundaries

`driver/` owns command options, source collection and loading, diagnostic
presentation, artifact output, and build invocation.
The executable entry in `src/carven.cpp` calls `driver.cli`, which normalizes
process arguments through `support.process` and dispatches commands. The formatter
uses the same argument adapter. Both command drivers retain UTF-8 storage while
command parsers borrow string ranges. `support.process` also reads UTF-8
environment values. The Standard Craft process provider queries host arguments,
executable paths, and the host platform. Executable lookup preserves the selected
driver name. The compiler provider resolves the running executable's real path
and uses the Crafts directory beside its binary directory, or the containing
source checkout for development binaries. The standard build library selects
`CXX` and applies the default compiler name.
`driver.sources` inventories matching toolchain files and explicit inputs,
deduplicates canonical paths, and sorts by absolute physical path. Explicit inputs
are analysis roots. `frontend.program.dependencies` follows their source
prerequisites through available parsed documents; drivers, hosting, and workspace
navigation share this closure. The driver reads only reached source files and
passes the resulting `ParsedDocument` objects into analysis without reparsing.
Retained checked definitions supply dependencies without source loading.

`driver.compile` shares input loading, C++ generation, and artifact writing between
the CLI and the Standard Craft operation declared in `carven/std/compiler/native.hpp`.
Each invocation owns a separate semantic program and executes its static roots.
The function uses the matching installed Crafts and takes input and destination
paths; it returns owned implementation paths, source identities, link
dependencies, and cacheability.
`std::compiler` owns the source interface and native contract. Semantic execution
calls its implementation through the internal standard-capability adapter.
Source diagnostics and static output use the host's standard streams.
The CLI separately serializes its manifest for external build tools.
Native callers link the compiler implementation.

Explicit-source `build` and `run` commands invoke the standard build library's
source-plan function through the interpreter. Project commands interpret the
user's `build.cv`. Both use ordinary source loading, analysis, and execution.
The source library's `build(plan)` applies host-command selection around its
internal build operation. It returns typed artifacts and, for `carven run`, runs
the selected executable before returning.

The loaded source closure generates its own implementations. Modules supplied by
a retained dependency keep external implementation ownership. Artifacts preserve
that distinction separately from explicit roots, which select runtime tests and
inspection output.

`load_and_analyze_sources` prepares the selected batch and invokes analysis.
`diagnostics.report` renders diagnostic codes and source spans; the driver owns
terminal styling and transport.

Static and interpreted execution share `semantic.evaluation.standard`, which
adapts execution values directly to the compiled Standard Craft providers.
Its typed operation catalog supplies result types, capability checks, and native dispatch.
Native implementations compile in `carven-modules` alongside the command layer.

Native hosts include the C++23 interfaces under `include/carven/hosting/` and
link the compiler objects. The headers expose owning boundary values, opaque
program and session state, and typed bindings. Source loading and replacement
use the compiler's analysis and interpreter in the host process. Implementations
in `src/hosting/` adapt public values and services to internal execution.
Internal drivers and standard capabilities use compiler module interfaces.
Shared native type definitions retain their global-module identity. Semantic
execution includes `runtime/hosting/object.hpp` directly and retains native object identity.
Compiler semantic types and execution storage remain internal. Generated
programs use the C++20 runtime support supplied by their dependencies.

The primary module contains only `export module carven;`. Compiler contracts and
implementations use internal partitions, and consumers directly import the
declarations they use. Native hosts compile against the public headers and link
the implementation objects without importing the compiler module or loading its BMIs.

`ExecutionHostObject` holds the shared Runtime `ObjectRef` by value.
`semantic.evaluation.value` includes its header after `import std`, inside an
unexported `extern "C++"` block. This preserves the header's global-module identity
and avoids Clang's unreachable standard-library definitions when the partition
is imported indirectly. The header supplies its dependencies for ordinary C++ use.

`HostValue` owns values crossing the public hosting boundary. `ExecutionValue`
also carries semantic identities, execution storage coordinates, and cold
operations. Their conversions and contract checks belong to `src/hosting/`.
Ordinary execution-value member implementations belong to `value.cpp`.

The standard process provider owns child lifecycle and communication. The Standard
Craft build model owns project fields, target source selection, cache validity,
native scheduling, and linking.

Module implementations report their provenance source;
synthetic artifacts have no source owner.

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
| `crafts/carven/runtime/hosting/` | C++20 native object identity, typed service contracts shared by execution paths, with native-only value/async adapters |
| `driver/` | Command orchestration, source loading, toolchain discovery, diagnostics, and build invocation |
| `include/carven/hosting/api.hpp` | Program, session, call, object, and service contracts |
| `include/carven/hosting/call.hpp`, `arguments.hpp` | Native call contracts, result delivery, and owning argument decoding |
| `include/carven/hosting/entry.hpp` | Native entry invocation and generated entry contracts |
| `semantic/evaluation/standard` | Standard capability checks, native dispatch, and direct execution-value conversion |
| `include/carven/hosting/operation.hpp` | Ordinary C++ operation bindings |
| `include/carven/hosting/method.hpp` | Native object method binding |
| `include/carven/hosting/service.hpp` | Synchronous and asynchronous host service binding |
| `include/carven/hosting/request.hpp` | Typed async completion mailboxes and cancellation |
| `hosting/` | Private programs, invocation state, value validation, requests, and events |
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

Compiler-internal callback ports use `FunctionRef<R(Args...) noexcept>` from
`support.function_ref`. It copies function pointers and borrows lvalue callables.
Borrowed objects must outlive every invocation, including after coroutine
suspension. Empty views represent optional recipients and must not be invoked.
Public hosting callbacks declare their context and function pointer explicitly.
Escaping exceptions terminate at the callback boundary.

Within the pipeline, `draft` denotes `ProgramDraft`, `semantic` denotes published
`SemIRProgram`, and `compilation` denotes `PlannedCompilation`, which owns a target
plan and borrows the program. Keep source-module and semantic-module identities,
failure terms and solved sets, and control-flow completion distinct.

## Publication gates

`ProgramDraft::finish()` in `analysis.program` consumes the draft through these gates:

1. Require completed declaration and body reservations, including nominal
   instances and callable results. Catalog and import-use state end before solving.
2. Solve failures, call-stop effects, and types; finalize signatures,
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
compiler-provided contracts. Types such as `ReadResult<T>`, `Duration`, `TimeRangeError`,
`Timeout`, parse and UTF errors, input handles, and time points are standard-source
declarations. Compiler-recognized operations obtain those declarations through
ordinary catalog lookup and completion. Checked text construction, input error
mapping, and public waits are source bodies; host input, clock sampling, and
timer waits use module-local provided operations.

Semantic execution uses compiled standard providers through its internal
capability adapter. Construction and publication consume semantic contracts.
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
