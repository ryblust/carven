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
Observed types also retain detached display strings, formatted once per construction
type during observation delivery. Editor queries can consume these descriptions
without borrowing construction rows, including from failed source-query analysis.
Callable observations may retain a proven target as a source span, separate from
the binding's declaration identity. Concrete function/closure types and immutable
view initializers supply this evidence; signature-only views do not. Closure
anchors and external-name/import descriptions remain detached source information,
including during source-query recovery; they do not publish construction IDs or
parse external C++.
Explicit native import origins are retained separately from C++ entity identity
for source navigation. Completion enumerates admitted explicit bindings with
neutral kinds and known paths; namespace openings do not enumerate external
symbols. These observations reuse catalog lookup precedence and source spans.

Output capture and source observations are independent options; output can be
forwarded synchronously without retaining a copy.

`ParsedDocument` owns one source revision, its token buffer, recovered syntax, and
diagnostics. Lexical editor queries borrow the retained tokens; syntax binding
rebinds AST coordinates for a compilation without changing the document's tokens.

`compiler.compile` analyzes sources, then calls `generate_artifacts` with the
published program. The driver loads or analyzes inputs. Check optionally retains
semantics through `-o`; compile generates C++. Interpret executes the published
program; dump consumes lexical or syntax results directly. Build and run execute
Standard Craft build scripts, which invoke the compiler CLI and schedule
native tools. Errors prevent program delivery; warnings accompany successful results.

Checking sources with an output destination also retains `CheckedDefinitions`: completed
construction rows containing generic and staged bodies for consumer
instantiation. Both definitions and the executable program are delivered only
after successful semantic publication. Ordinary and test C++ generation share
that program without repeating analysis or static execution.

`compiler.artifact` packages the program, optional checked definitions, module
ownership, and cacheability. Source identities remain in semantic provenance.
An artifact-directory input alone supplies the program to the backend; checked
Craft artifacts accompanying new sources supply definitions to construction
and specialization. `compiler.manifest` projects inputs, artifact inventory,
implementation dependencies, toolchain resources, owned runtime-test count, and
cacheability for build integration.

## Source and command boundaries

`driver/` owns command options, source collection and loading, diagnostic
presentation, artifact output, and build invocation.
The executable entry in `src/carven.cpp` selects standard execution bindings and
passes them to `carven_main`. `driver.cli` normalizes process arguments through
`support.process` and dispatches commands. The formatter uses the same argument
adapter. Both command drivers retain UTF-8 storage while
command parsers borrow string ranges. `support.process` also reads UTF-8
environment values, resolves the running executable, and runs child processes
for formatter forwarding and Craft queries. These compiler operations use OS
facilities directly, independently of Standard Craft providers and runtime values.
The Standard Craft process provider supplies process operations to Carven programs;
execution frontends link it for interpretation and constant execution.
CLI, workspace analysis, and Hosting each bind the providers needed by the Carven
code they execute. Compiler filesystem, environment, and subprocess operations
belong to `support`; they do not call Standard Craft providers. Similar platform
code alone does not require a shared adapter or a dependency between these layers.
The compiler driver uses the Crafts directory beside its resolved binary directory,
or the containing source checkout for development binaries. The standard build library selects
`CXX` and applies the default compiler name.
`driver.sources` inventories matching toolchain and project Craft files,
deduplicates canonical paths, and sorts by absolute physical path. Explicit inputs
are analysis roots; inventory entries are dependency candidates.
Ordinary project modules are located by canonical module path when imported.
`frontend.program.dependencies` follows their source
prerequisites through available parsed documents; drivers, hosting, and workspace
navigation share this closure. The driver reads only reached source files and
passes the resulting `ParsedDocument` objects into analysis without reparsing.
Retained checked definitions supply dependencies without source loading.
Every Craft has a native linkage identity derived from its canonical Craft name,
so standard and project Crafts can supply retained dependencies by the same rules.

`driver.compile` owns CLI input loading, C++ generation, artifact writing, and
manifest publication. Each source compilation owns its semantic program and
executes its static roots. The manifest describes implementation paths, source
identities, link dependencies, cacheability, include roots, candidate directories,
and participating Crafts' ordinary `build.cv` query results. An empty compilation publishes only
these resources, without analyzing sources or generating C++.
`std::compiler` invokes this CLI through `std::process` and reads the manifest
through `std::fs` and `std::json`. Interpreted and generated clients use this same
source implementation; source diagnostics and static output retain the child
process's standard streams.

Explicit-source `run` invokes the standard build library's source-plan function
through the interpreter. Its inputs form one executable target. Project commands
interpret the user's `build.cv`; `build` accepts no source arguments. Both paths
use ordinary source loading, analysis, and execution. The source library's
`project(plan)` dispatches build, run, or query over one resolved plan.
`build(plan)` always builds and returns typed artifacts, independently of the
host command.

The loaded source closure generates its own implementations. Modules supplied by
a retained dependency keep external implementation ownership. Artifacts preserve
that distinction separately from explicit roots, which select runtime tests and
inspection output.

`load_and_analyze_sources` prepares the selected batch and invokes analysis.
`diagnostics.report` renders diagnostic codes and source spans; the driver owns
terminal styling and transport.

Compiler analysis and interpretation accept a native execution callback. Ordinary
C++ delegation uses source contracts and leaves unresolved C++ operations to the
native compiler; it does not query a host registry for standard-library types.
Static execution requires a selected provider only when it reaches a native call.

`carven-modules` is a `moduleonly` target containing `src/` except the executable
entry `src/carven.cpp`. That entry selects execution providers linked by the
`carven` target; other core consumers select their own providers. The core owns
analysis, interpretation, and static
execution. `semantic.evaluation.native` supplies internal typed native calls;
the core does not depend on SDK headers or public `Host*` types.

`driver.cli` passes the supplied bindings explicitly to command execution;
workspace analysis uses the same internal calling capability.
`crafts/carven/std/providers.hpp` lists the standard functions once for CLI and SDK
consumers. Each derives its bindings and links the native implementations it uses.

The `carven-hosting` static library depends on the core and owns the public value,
service, program, session, and event adapters. Its optional `host_standard_services()`
factory uses the same standard function list. Hosting sessions use only their
supplied bindings. CLI, workspace, and analyzer tools do not depend on this SDK,
and the default compiler build does not build it.

Native hosts include the C++23 interfaces under
`tools/hosting/include/carven/hosting/` and link `carven-hosting`. The installed
library includes the required core and native
implementations; consumers need its public headers and a compatible C++ toolchain,
without compiler BMIs or a separate core library. The host's binary or library
target links its own native providers and generated entries.

These headers are the sole declarations of the public API. Implementations in
`tools/hosting/src/` include them in global module fragments and import internal
compiler partitions. Compiler semantic types remain internal; header-defined
opaque state retains global-module identity.
Generated programs use the C++20 runtime support supplied by their dependencies.

The primary module contains only `export module carven;`. Compiler contracts and
implementations use internal partitions, and consumers directly import the
declarations they use. Native hosts compile against the public headers and link
the hosting library.

`ExecutionHostObject` holds the shared Runtime `ObjectRef` by value.
`semantic.evaluation.runtime` includes the shared execution headers once after
`import std`, inside an unexported `extern "C++"` block. This preserves their
global-module identity and avoids Clang's unreachable standard definitions and
duplicate platform declarations across internal partitions. The headers supply
their dependencies for ordinary C++ use.
Core execution also uses Runtime vector storage and the shared hash kernel; these
headers remain explicit native dependencies.

`HostValue` owns values crossing the public hosting boundary. `ExecutionValue`
also carries semantic identities, execution storage coordinates, and cold
operations. Their conversions and contract checks belong to `tools/hosting/src/`.
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

`parse_recovering` retains complete imports, items and statements around recoverable
syntax errors for source queries. Import failures recover at the next import or
declaration; unfinished selection lists retain their completed entries. Missing
parentheses and brackets recover at statement, enclosing delimiter and end-of-file
boundaries. Braces preserve nested declaration ownership. Lexical errors retain
invalid opaque token regions, and `ParsedDocument` continues recovering parsing.
Single-line literals stop before a physical line ending, including after a trailing
backslash. Unterminated multiline literals seek a later statement/declaration line
or closing brace. Failed interpolation rolls back its tokens and literal values
together. Irreconcilable delimiter pairing and excessive nesting can prevent a tree.
Ordinary parsing rejects results with diagnostics. `parse_program` reuses retained
documents or parses missing inputs, validates source bindings, rebinds diagnostics,
and constructs fresh provenance
and resolved imports. Recovered syntax does not enter compilation.

## Source queries

`semantic.analysis.source` delivers optional `SourceAnalysisOutput` batches with
selected entities, alias binding origins, and types at source tokens. Aliases
navigate to bindings; source entities navigate to declarations. References retain
local spellings; provided exports supply module metadata. Successful publication
supplies program-owned types. Failed analysis can retain definitions and known
builtin types from admitted declarations and successfully analyzed portions of
failed bodies, including nested closures. These observations do not establish
program validity. Recipients copy records they retain; empty recipients perform
no recording.

An explicit `CompilationAnalysisOptions::completion` source position asks the same
construction to collect visible name/member candidates. `SourceCompletionRequest`
borrows a synchronous recipient; retained candidates own their spelling and type
descriptions, with no construction IDs. Name lookup, type resolution, and member
interpretation report candidates at that position even if subsequent construction
fails. `source_query` analysis binds the same immutable recovered syntax and can
isolate known failed body statements. Unavailable locals retain binding identity
and shadowing, but receive no invented type. Explicit annotations are recorded
before initializer analysis, so an invalid initializer does not erase an established
declared type. A body containing isolated statements remains unavailable for
publication and inferred callable contracts. This mode never delivers a program,
checked definitions, or construction TypeIDs.

Recovery never removes imports or module declarations to manufacture a closed
scope. The recovering parser retains the known names of damaged declarations,
making only those names unavailable. Unknown declaration heads or unfinished
import selections mark the module scope incomplete; lookup and completion do not
claim a unique binding from missing candidates. Independently retained syntax
remains available to outlines.
This conservative boundary does not yet model individually uncertain import edges
or a general partial type graph.

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
| `src/driver/` | Command orchestration, source loading, toolchain discovery, diagnostics, and build invocation |
| `semantic/evaluation/native/` | Internal typed native calls for analysis and interpretation |
| `crafts/carven/std/providers.hpp` | Standard native function and conversion inventory |
| `tools/hosting/include/carven/hosting/` | Public C++23 values, native bindings, programs, sessions, events, and entry contracts |
| `tools/hosting/src/` | Public API adaptation, hosted execution, value conversion, requests, and lifecycle |
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
references. Each body has one owner: a callable, a test, or a module-scope `const`
block.

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

Source observations report construction completeness separately from publication.
The marker is set only after construction and solving have completed; later global,
async, ownership or nullability diagnostics can still reject the program. Complete
observations retain source bindings, detached type descriptions and callable targets
without exposing unpublished TypeIDs. A complete source index therefore does not
authorize generation, execution or extraction of a checked unit. Partial declaration,
body and solving failures continue to report incomplete source construction.

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

Semantic execution reaches selected native providers through the native-call callback.
Construction and publication consume semantic contracts.
Generated programs use native support owned by each capability, including
networking socket resources. Source wrappers obey ordinary type, access,
ownership, failure, and execution rules in both semantic and generated execution.
Native imports retain their delegated contracts.

Source completion records the query context (name, type or member) alongside
detached candidates. `semantic.analysis.source.imports` enumerates importable source
declarations from retained syntax and shares the catalog's declaration-visibility
mapping. Tooling supplies the selected module set and plans source edits; compiler
module paths and provided-export metadata remain authoritative.

## Diagnostics and dependencies

Produce diagnostics at the rule owner. Source operations retain origins;
implicit operations use linked expansion origins. Body construction continues
past failed functions, tests, and `const` blocks. Record a failed function once;
later requests fail without repeating its diagnostic. Stages requiring completed
bodies run only after construction reports no error. Compiler-private invariant
failures terminate at the violated boundary.

Diagnostic attachments may carry titled fixes with source replacement spans.
The rule owner supplies concrete edits alongside its finding; tooling must not
infer edits from rendered messages or help strings. Retained document diagnostics
rebind fix spans together with labels and notes when selected into a compilation.
Import usage records selected local spellings as well as whole bindings, allowing
unused-import diagnostics to identify individual explicit selections. Workspace
exposes fixes against the original source selection; editor clients
validate versions and apply the edits.

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
