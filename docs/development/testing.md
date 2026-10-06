# Testing

Test current contracts: accepted behavior, rejected inputs, and invariants at
their owning boundaries. Assertions admit equivalent implementations that
preserve the observed property. Add cases for missing evidence, including test
infrastructure contracts that product suites do not establish.

## Test responsibilities

| Group | Boundary | Evidence |
| --- | --- | --- |
| `internal` | Compiler modules and runtime support | Semantic rules, diagnostics, representation invariants, planning, serialization, and runtime operations |
| `language` | Compiled and executed Carven programs | Values, access, ownership, control, failure, modules, closures, and testing behavior |
| `crafts` | Public source-package APIs | Library results, errors, state transitions, and algorithms |
| `interop` | C++ providers, consumers, and support headers | Boundary signatures, native calls, source fragments, and header self-containment |
| `examples` | User-facing programs | Documented output from actual example executables |
| `cli` | Compiler processes, interpreted execution, and build integration | Arguments, reports, exit status, files, scheduling, and generation policy |
| `formatter` | Source formatting and file operations | Source preservation, layout, errors, batch results, and replacement |
| `workspace` | Document snapshots and cached source queries | Source observations, result ownership, versions, and invalidation |
| `analyzer` | Resident analysis sessions and process messages | Atomic requests, owning responses, message encoding, transport, and process lifetime |

Place each case in the group that owns the tested boundary. Reuse fixtures and
assertions across applicable configurations. Merge cases that repeat the same
input class, execution path, and observation.

Internal C++ tests use the compiler configuration. Generated-source and consumer
tests use the C++20 baseline, with additional standard modes for native capability
branches. Shared semantic cases use shared sources across these modes.

Static execution, interpreted execution, and generated native C++ are separate
boundaries. Keep representative cases at each applicable boundary. Static tests
establish compile-time contracts; native tests observe storage, lifetimes, and
runtime state. Interpreter cases cover admission, failure propagation, and budgets.

Language tests use local Carven state for counters and traces. A native provider
may expose observations Carven cannot express; cases whose subject is that native
boundary belong in `interop`. User-facing programs live under `examples/`;
diagnostic and termination cases belong to test suites.

Crafts tests mirror public package APIs. Portable and accelerated implementations
establish the same functional contracts. Consumer targets select instruction-set
options for accelerated tests on machines that support them.

## Evidence and assertions

Internal C++ tests use the shared harness in `tests/harness/` and import only
the framework and domain fixtures they need. Generated programs use Carven's
testing support. Follow neighboring suites for registration and assertion syntax.

Test names identify the owning component and observed behavior. Name independent
inputs and preserve their identity in failure context. Scoped observations and
ordinary traversal can remain within one case. Case tables contain inputs; empty
values remain valid where the tested contract permits.

Use comparison assertions for printable values and predicates with context for
opaque values. Compare text contents and retain explicit lengths for binary data.
Compute expensive failure context only when needed.

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
small configured limits establish accounting, and production defaults establish
source diagnostics and traces. Large inputs serve a stated scale-dependent
property. Semantic limit cases stop at analysis and publication; backend cases
cover generated operations. Independent expensive contracts have separate results
and timeouts.

Lifecycle tests observe construction, transfer, execution, and destruction,
including conditional paths and failure cleanup. Cover interactions among copying,
mutation, scope exit, and calls for captures and views. Compile-time checks or
diagnostic cases establish static restrictions; compiled uses of runtime helpers
exercise the native capabilities required by the operation.

Performance comparisons use equal results, effects, and output contracts. Define
measurement boundaries, workloads, configuration, and sampling conditions before
comparing implementations. Measure the costs named in the claim separately and
limit conclusions to that evidence. Keep raw logs outside reference documents.

## Organization and fixtures

Each group owns its `xmake.lua`. Topic directories name the rule under test;
features and scenarios belong in filenames and case names. Helpers remain with
their owning domain; harnesses select scenarios.

Extend existing targets when entry definitions, compiler settings, linkage, and
execution requirements match. Use ordinary Xmake targets and assertions for
execution and output checks. Custom harnesses supply additional process,
diagnostic, or artifact observations.

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

Native rejection fixtures establish successful Carven generation before checking
the expected diagnostic, subject, and source attribution. Accepted counterparts
use the normal interop build. Rejections compile on each run and retain artifacts
on failure.

Header self-containment checks use one translation unit per header within its
consumer target and instantiate its interface. Behavioral observations belong in
runtime or generated-program tests.

Terminating operations run in isolated processes with a specific expected outcome.
Validate setup before entering a death-test action; the action contains only the
production operations under test. Assertions inside it can abort and falsely
satisfy the observation.

Native exception-boundary cases execute the throwing operation and verify that it
reaches the installed termination handler; escaped exceptions are failures.
Allocation-failure injection uses separate executables and reaches the allocations
under test, including those across library linkage boundaries.

## Validation

Commands run from the repository root through `./xmakew`; Windows uses
`.\xmakew.ps1` with the same arguments.

| Command | Capability |
| --- | --- |
| `./xmakew build` | Build or update the local compiler |
| `./xmakew test -g <group>` | Build and run one group from the responsibility table |
| `./xmakew test` | Build and run all registered tests, including examples and `carven-format` |
| `./xmakew build carven-format` | Build the formatter used by formatting tasks |
| `./xmakew format-check` | Check C++ and Carven formatting without changing sources |
| `./xmakew format` | Apply C++ and Carven formatting |
| `./xmakew clean` | Clear build outputs |

Generated-test preparation invokes the local compiler before target dependencies
are built. `./xmakew build` supplies an up-to-date compiler before testing.
Formatting tasks require a built `carven-format`. Applying formatting changes sources;
subsequent builds and tests use those changes. A clean followed by a build recreates
module build state.

Static analysis is available through `./xmakew check clang.tidy` for handwritten
and generated C++. `.clang-tidy` configures handwritten checks;
`xmake/generated.clang-tidy` configures generated-code checks.

`.github/workflows/ci.yml` defines CI platforms, sanitizer configurations, and
static-analysis jobs. Sanitizer coverage includes native compilation launched
outside build targets. Targets and harnesses own test selections and timeouts.
