# Testing

Tests establish accepted behavior, rejected inputs, and invariants at language
and toolchain boundaries.
Choose cases by the failure they can detect, rather than by the number of files,
classes, or internal components they exercise. An internal component does not
need its own suite when existing behavioral tests establish its responsibility.

Each case should identify an observable contract, a plausible failure, and the
evidence it adds beyond existing cases. Assertions admit equivalent implementations
that preserve that contract. Keep focused internal tests when they isolate a
complex algorithm, cheaply exercise a difficult boundary, or establish a contract
that broader tests cannot adequately expose. Avoid mirroring private data layouts,
helper call sequences, or standard-library behavior.

Native program tests are ordinary consumer targets using the packaged
`@carven/carven` rule. The build system owns source selection, target
configuration, generation scheduling, artifact publication, and native build
caches.
Use these targets to establish language values, effects, lifetimes, and native
ABI behavior. A compiler-process case establishes a CLI contract; it does not
repeat native behavior already covered by a consumer target. Keep one
representative direct-run case for its argument, process, and installed-toolchain
boundary.

Generation tests observe artifact paths, source attribution, raw bytes, and
interface stability. Incremental workloads observe artifact changes across edits
and validate the resulting programs. Exact target-node counts, temporary names,
selected runtime helpers, and optimization-specific storage layouts are not
contracts. Focused scale cases may bound growth for deep or shared input graphs
without fixing the generated representation. Source rejection tests exercise
the public compilation boundary instead of reproducing semantic helper decisions.

## Test responsibilities

| Group | Boundary | Evidence |
| --- | --- | --- |
| `internal` | Focused compiler boundaries | Algorithmic boundaries, diagnostics, and invariants not adequately established by program-level tests |
| `language` | Compiled and executed Carven programs | Values, access, ownership, control, failure, modules, closures, and testing behavior |
| `crafts` | Public source-package APIs | Library behavior through C++20 consumer builds |
| `interop` | C++ providers and consumers | Boundary signatures, native calls, and source fragments |
| `examples` | User-facing programs | Documented output, application behavior, and connected example workflows |
| `cli` | Compiler processes and interpreted execution | Arguments, reports, exit status, source selection, and emitted files |
| `format` | Source formatting and file operations | Source preservation, layout, errors, batch results, and replacement |
| `workspace` | Source snapshots and analysis queries | Versioned locations, complete selection reuse, invalidation, and navigation |

Place each case in the group that owns the tested boundary. Reuse fixtures and
assertions across applicable configurations. Merge cases that repeat the same
input class, execution path, and observation. Prefer one behavioral case at
the owning boundary over assertions on intermediate node kinds, temporary storage,
helper names, or enumeration-to-enumeration mappings.

Language and execution-host contracts belong to `language`; Standard Craft
algorithm behavior belongs to `crafts`. Declaration ownership, a `std::` module
path, or an implementation language does not choose the test group. Native
provider and consumer contracts belong to `interop`.

`interop/toolchain-consumer` checks installed Crafts, native interoperation, and
incremental consumer builds. Run it with `./xmakew test '*/toolchain-consumer'`.
Package installation also needs validation with a fresh consumer using the
default package configuration and an isolated build/install tree.

Group behavior in one executable per native configuration and runner contract.
Register fresh process scenarios on that executable when input state or
termination requires isolation. Separate targets express different compiler
settings or entry contracts. Preserve values, effects, cleanup, and exit status
when merging sources.

Choose execution partitions independently of build targets: long suites can run
disjoint selections in parallel without compiling and linking separate binaries.
Measure both individual duration and queue delay under a fixed test job limit.
Keep partitions coarse enough that process startup and repeated fixture setup
remain small relative to the work. A short group need not be partitioned.

Standard-input scenarios share the language executable but use fresh processes:
EOF, errors, and buffered bytes persist within one process. Printing backend
branches, escaped entry failures, and deliberately failing runners have distinct
native configurations or entry contracts. Independent stream inputs use separate
processes; pipeline steps run sequentially because each consumes the preceding
program's output.

Test current API behavior and failure contracts at the owning boundary.

Use current language and standard APIs in fixture setup and incidental code.
Preserve explicit forms when syntax, imports, evaluation, native interoperation,
or formatting is the behavior under test. Simplification must retain that evidence.

Internal C++ tests use the shared harness and compiler configuration. Crafts,
generated-source, and consumer tests use the C++20 baseline, with additional
standard modes for native capability branches. Shared semantic cases use shared
sources across these modes.
Compiler processes for self-contained language, diagnostic, reporting, and
artifact fixtures use `--explicit-inputs` and name their complete source set.
This keeps their analysis cost proportional to the fixture. Source-discovery,
installed-toolchain, and representative ordinary-command tests use normal
discovery and establish Crafts integration.
Default CLI cases include the bundled standard sources and a mixed Craft.
Observe successful Carven execution with unused native code and a source-located
diagnostic when an unsupported native operation executes. Native initialization
effects belong to native execution; test that boundary through process output.

Use an independent public source module in CLI installation fixtures for
installed-path discovery and source identity. Supply production runtime headers
to the native execution fixture for native providers and process behavior.
Exercise library APIs through ordinary Crafts and example targets.

Static execution, interpretation, and generated native C++ implement the same
language contracts. Use representative cases across these execution contexts;
keep broad value tables and offset matrices in native tests. Add a context-specific
case when it observes a distinct responsibility, such as static result retention,
interpreter budgets, native storage, or host-resource interaction. Reuse source
functions where possible instead of multiplying complete matrices.

Language tests use local Carven state for counters and traces. A native provider
may expose observations Carven cannot express; cases whose subject is that native
boundary belong in `interop`. User-facing programs live under `examples/`;
diagnostic and termination cases belong to test suites.

Crafts tests mirror public package APIs. Portable and accelerated implementations
establish the same functional contracts. Consumer targets select instruction-set
options for accelerated tests on machines that support them. The portable matrix
covers APIs with a distinct portable implementation; ordinary library behavior
runs once.

## Evidence and assertions

Internal C++ tests use the shared harness in `tests/harness/` and import only
the framework and domain fixtures they need. Generated programs use Carven's
testing support. Follow neighboring suites for registration and assertion syntax.

Test names identify the owning component and observed behavior. Name independent
inputs and preserve their identity in failure context. Scoped observations and
ordinary traversal can remain within one case. Case tables contain inputs; empty
values remain valid where the tested contract permits.

Independent compilations can use separate ordinary cases with captured inputs.
Each case owns its callback and any source text it needs, so process shards can
select those inputs independently. Use `each` within one case when observations
share state, source identity, or a dependent sequence of operations.

Use comparison assertions for printable values and predicates with context for
opaque values. Compare text contents and retain explicit lengths for binary data.
Compute expensive failure context only when needed.
Compare optional results directly with values or `std::nullopt`; the harness
prints their state and recursively prints present payloads.

When tests repeat observation or diagnostic boilerplate, improve the shared
assertion or domain fixture at its owning boundary. Keep helpers focused on
recurring checks and preserve useful failure values, source locations, and
explicit control over dependent observations.

Guard premises before indexing, dereferencing, or reading an error. When a premise
assertion fails, explicitly return from the current case or input before dependent
observations. Nonfatal assertions record failure and continue execution. In the
internal C++ harness, a failed `require` terminates the process; reserve it for
unrecoverable fixture construction.

Registered test targets must build successfully. Drivers return success when
assertions pass, including expected rejection or termination. Compile-only targets
verify compilation and linking; generated runners also report that cases ran and
passed.

Check valid and malformed states at the boundary that owns the invariant. Assert
structured violations when available. Diagnostic tests compare identity, severity,
and relevant source attribution; presentation tests also check wording and transport.

Check generated-code behavior by compiling and executing it. Exact text belongs
to serialization, source attribution, raw payload preservation, artifact paths,
and other byte-level contracts. Avoid assertions on temporary names, helper
spellings, or complete generated bodies.

Structure assertions establish properties such as bounded growth or single
execution. Scope cost assertions to the measured operation and state the bound.
Optimization cases check required effects and the work removed without fixing
the complete emitted form.

Execution-selection tests use valid, terminating operands and observe call counts
or traces. Invalid inputs and resource limits have separate diagnostic cases.
Resource accounting exercises actual operations at accepted and rejected limits;
small configured limits establish accounting. Representative recursion limits
establish source diagnostics and traces. Large inputs serve a stated
scale-dependent property; routine tests do not exhaust production iteration,
instance, or expanded-node budgets. Semantic limit cases stop at analysis and
publication; backend cases cover generated operations.

Lifecycle tests observe construction, transfer, execution, and destruction,
including conditional paths and failure cleanup. Cover interactions among copying,
mutation, scope exit, and calls for captures and views. Compile-time checks or
diagnostic cases establish static restrictions; compiled uses of runtime helpers
exercise the native capabilities required by the operation.
Async fixtures establish ordering through observable progress at the tested
boundary. Fixed sleeps do not establish that an input was consumed or an operation
suspended; timeouts guard stalled tests.

Performance comparisons use equal results, effects, and output contracts. Define
measurement boundaries, workloads, configuration, and sampling conditions before
comparing implementations. Measure the costs named in the claim separately and
limit conclusions to that evidence. Keep raw logs outside reference documents.

## Organization and fixtures

Each group owns its `xmake.lua`, including its integration targets. The project
root includes groups; it does not register their internal scenarios or subdirectories.
Topic directories name the rule under test;
features and scenarios belong in filenames and case names. Helpers remain with
their owning domain; harnesses select scenarios. Filenames and local type names
avoid repeating context already supplied by their directory or module, such as
`async/failure.cv` and its `Failure` type. Keep qualifiers where a flat directory
or shared namespace needs them to distinguish responsibilities.

Extend existing targets when entry definitions, compiler settings, linkage, and
execution requirements match. Run ordinary async scenarios through a shared
driver. Keep distinct entry contracts, escaped failures, and cancellation in
separate processes. Use ordinary Xmake targets and assertions for execution and
output checks. Pure C++ consumers include runtime headers directly and do not
attach the Carven source-generation rule. Custom harnesses supply additional
process, diagnostic, or artifact observations.

Cases initialize their own observable state. Temporary directories and captured
streams use fresh paths for each invocation, including concurrent runs. Fixtures
own setup and observations; file-operation cases use independent I/O to prepare
and observe bytes. Process harnesses bound execution time, retain failed artifacts,
and clean up successful temporary state.

Fixtures with significant source bytes use `.cv.fixture` inputs copied to source
files by their harness. Behavioral tests use ordinary sources and avoid fixed
line numbers unless source attribution is the contract. Formatting fixtures check
source preservation, expected output, idempotence, and stability under irrelevant
whitespace changes. Corpus tests establish formatting, parsing, and idempotence
for repository programs.

Independent CLI invocations register separate named Xmake tests even when they
share fixture bytes. Each receives a fresh working directory. Keep sequential
steps when later observations depend on earlier files or process effects.
Xmake's test job limit controls concurrent invocations; avoid an internal serial
case list becoming the critical path of the whole group.

Native rejection fixtures establish successful Carven generation before checking
the expected diagnostic, subject, and source attribution. Accepted counterparts
use the normal interop build. Rejections compile on each run and retain artifacts
on failure.

Exercise runtime behavior through generated C++20 consumers in Crafts, language,
and interop tests. Place direct C++ observations of native signatures, inputs,
and storage validity in interop.

Terminating operations run in isolated processes with a specific expected outcome.
Validate setup before entering a death-test action; the action contains only the
production operations under test. Assertions inside it can abort and falsely
satisfy the observation.

Native exception-boundary cases execute the throwing operation and verify that it
reaches the installed termination handler; escaped exceptions are failures.

## Validation

Commands run from the repository root through `./xmakew`; Windows uses
`.\xmakew.ps1` with the same arguments.

| Command | Capability |
| --- | --- |
| `./xmakew build` | Build or update the local compiler |
| `./xmakew test -g <group>` | Build and run one group from the responsibility table |
| `./xmakew test` | Build and run all registered tests, including examples, formatting, and workspace analysis |
| `./xmakew build carven-format` | Build the formatter used by formatting tasks |
| `./xmakew format-check` | Check C++ and Carven formatting without changing sources |
| `./xmakew format` | Apply C++ and Carven formatting |
| `./xmakew clean` | Clear build outputs |

Generated-test preparation invokes the local compiler before target dependencies
are built. `./xmakew build` supplies an up-to-date compiler before testing.
Formatting tasks require a built `carven-format`. Applying formatting changes sources;
subsequent builds and tests use those changes. A clean followed by a build recreates
module build state.

Measure compiler build, generated-source preparation, native compilation,
incremental checks, and test execution separately. Xmake records execution time
per registered case. Harness-driven native compilation and rejection checks run
on each test invocation and contribute to that time; ordinary consumer targets
reuse their build outputs.

For internal case costs, build `carven-test-internal`, then run
`./xmakew run carven-test-internal --timings`.
The report lists case execution durations from slowest to fastest.
Combine it with `--filter`, `--exclude`, or
`--test` to reproduce a selected workload; `--list-tests` lists names without
running them. These timings include each case's fixtures and assertions but
exclude process startup, test registration, and reporting. Normal runs do not
collect timings. Keep machine-specific timing logs outside reference documents.

`--shard INDEX/COUNT` selects an interleaved part of the sorted cases after name
filtering, with indices starting at 1. The six internal registrations partition
the complete selection; Xmake's test job limit controls their concurrency.
Use `./xmakew run carven-test-internal --shard 1/6 --timings` to measure one part.

`carven-test-internal` selects all cases under `tests/internal`. Fixture modules
belong to `carven-test-fixtures`; `carven-test-runner` supplies the entry object.
`carven-test-support` owns assertions, diagnostics, temporary directories, and
termination checks. Run the suite with `./xmakew test -g internal`.
Workspace scenarios use `carven-test-workspace`; run them with
`./xmakew test -g workspace`.

Static analysis is available through `./xmakew check clang.tidy` for handwritten
and generated C++. `.clang-tidy` configures handwritten checks;
`xmake/generated.clang-tidy` configures generated-code checks.

`.github/workflows/ci.yml` defines CI platforms, sanitizer configurations, and
static-analysis jobs. Sanitizer coverage includes native compilation launched
outside build targets. Targets and harnesses own test selections and timeouts.
