# Testing

This document defines repository test responsibilities, case design, fixtures,
and validation.

## Responsibilities

| Group | Boundary | Evidence |
| --- | --- | --- |
| `internal` | Compiler components | Analysis results, diagnostics, retention, algorithms, and publication |
| `language` | Compiled Carven programs and execution hosts | Values, access, ownership, control, recovery, modules, closures, and inline tests |
| `crafts` | Public source-package APIs | Library results through C++20 consumer builds |
| `interop` | C++ providers and consumers | Boundary types, native calls, source fragments, and resource ownership |
| `cli` | Compiler processes and interpretation | Arguments, exit status, reports, source selection, emitted files, and execution |
| `format` | Formatting and file operations | Preserved source, layout, batch results, and replacement |
| `workspace` | Source snapshots and queries | Versioned locations, selection reuse, invalidation, and navigation |

Choose the group by the tested boundary, independently of implementation language
or a `std::` module path. Keep application checks with their sources under `examples/`.
Validate standalone `build.cv` examples through the CLI group so the examples
remain usable without repository build files.

Use one build-and-run invocation to validate the standalone build example.
Check object reuse and invalidation with small C++ inputs through the build
Craft's object-compilation function. Same-Craft fixtures can exercise internal
declarations through ordinary module visibility. Compile library consumers with
their Crafts targets; reserve CLI interpretation for their hosted execution.

Use focused internal tests for algorithms and compiler results that program-level
execution cannot expose.

Reuse representative sources across static execution, interpretation, and native
execution. Keep broad value and offset tables in native tests. Add a separate
execution-context case for a distinct responsibility such as retained static
results, interpreter accounting, native storage, or host input.

## Targets and execution

Declare each group's targets in its own `xmake.lua`; include groups at the project
root. Use ordinary Xmake targets with `@carven/carven` for `.cv` sources. Include
runtime headers directly in pure C++ consumers. Mark tests, examples, benchmarks,
and development tools as non-default targets.

Group sources in one target per native configuration and entry contract.
Declare inline-only suites as binary targets with `tests = true`.
Keep example checks with their application declarations. Extend an existing
target when its entry, settings, and linkage fit. Add a separate target only for a
distinct native or entry requirement.

Multiple `add_tests` registrations normally share an executable; supplying per-test
`files` creates a derived build. Use fresh processes for independent stream inputs
and host state. Use a harness only for input setup, pipelines, or output and artifact
observations that ordinary registrations cannot express. Keep dependent pipeline
steps sequential. Check the specified exit status, output, and file effects.
Bound process duration, including isolated termination checks.

Pass built executable targets to the shared process checks. Use their Xmake
runtime environment when checking exit status, ordered output, and file effects.

Reuse the shared test entry and support modules for internal, formatter, and
workspace executables. Xmake controls test concurrency. Choose partitions by
execution cost; keep process startup and repeated fixture setup small relative
to the work.

Use process isolation when termination itself is the contract under test.
Distinguish the specified termination from unrelated crashes, sanitizer failures,
and timeouts. Do not manufacture invalid internal state solely to exercise assertion
handlers.

Use the internal termination module for isolated compiler cases. Let Xmake run
native termination scenarios as separate processes and check their output.
Construct internal fixtures in the child before observing the operation's
termination. Use the generated entry for ordinary inline suites.

Use the main project's compiler, installed Crafts, resolved rules, and native
configuration for consumer tests.

Check the same API contracts in portable and accelerated Crafts implementations.
Select instruction-set options on consumer targets where supported. Run ordinary
library behavior once; share sources for APIs with a distinct portable implementation.

## Evidence

Choose each case by its observable contract and the evidence it adds. Assert
values, effects, lifetimes, output, or structured results in normally completing
tests. Admit equivalent implementations that preserve those observations.

Keep assertions at the owning boundary. Prefer structured diagnostic results over
failed compiler invocations. Check diagnostic codes, severity, and source attachments
through compiler APIs where available. Check rendered bytes in presentation tests.
Check business failure and cancellation payloads, recovery, and cleanup in normally
completing tests.

Name tests by their observed behavior. Name independent inputs and retain those
names in failure context. Merge cases with the same input class, execution path,
and observation. Use separate cases for independent compilations; use `each` for
observations sharing state or a dependent sequence.

Exercise admission through the public call or loading boundary that owns it.
Keep representative failures for distinct contracts such as type, access, shape,
and lifetime. A second route to the same rejection needs independent evidence.

Use comparison assertions for values and contextual predicates for opaque objects.
Compare text contents with explicit lengths for binary data. Compare optional
results with their expected values or `std::nullopt`. Compute expensive failure
context only when needed.

Guard premises before indexing, dereferencing, or reading a result variant. After a
failed nonfatal premise assertion, return from the case before making dependent
observations. Reserve the internal harness's `require` for unrecoverable fixture
construction.

Observe control flow through values and local counters or traces. Observe ownership
through construction, transfer, mutation, execution, and destruction. Include
conditional paths and recovered failures. Use native fixtures only for observations
Carven cannot express. Use `async test` for asynchronous test bodies and await
operations directly; keep explicit async entry or native-driver fixtures when
that boundary is the contract under test. Use observable progress to establish
async order; do not infer suspension or input consumption from fixed sleeps.

Use exact text for output contracts, raw payload preservation, and serialization
encoding. Do not fix generated temporary names, selected helpers, complete bodies,
intermediate node kinds, or storage layouts. Test public native signatures by
compiling consumers and test generated behavior by running them.

State the bound or work avoided in each cost assertion. Use deep, wide, or shared
inputs when validating stack use or growth bounds. Scope measurements to the
operation under test. Keep ordinary resource-accounting cases small; do not exhaust
production budgets in routine tests. Reserve boundary-sized inputs for the
size or depth limit itself; use small inputs for mutation and lifetime behavior.

Check retained generic and staged definitions, source ownership, and ordinary and
test generation in artifact-reuse cases. Observe static effects to verify that
retained generation does not execute analysis again. Distinguish retained results
from fresh input or time observations in host-effect cases. Measure work across
edits and execute the resulting programs in incremental workloads.

## Fixtures

Initialize observable state in each case. Give concurrent invocations independent
temporary directories and streams. Retain failed artifacts and remove successful
temporary state. Reuse a working directory only when later steps depend on earlier
files or effects.

Use ordinary source files for behavioral tests. Use `.cv.fixture` where fixture bytes
are the subject. Fix source positions only when attribution is the contract.
Use bare Carven declarations for helpers shared within a test project, hosted
entries, and their source types. Use `export` for cross-craft APIs or cases whose
contract exercises exported declarations. Use `export(cpp)` for generated entry
points called by C++ consumers; their signature types may remain bare. Exercise
C++ signature dependencies independently of cross-craft source visibility.
Check expected bytes, source preservation, idempotence, and stability under irrelevant
whitespace in formatter fixtures. Format and parse repository programs in corpus tests.

## Validation

Run commands from the repository root through `./xmakew`; use `.\xmakew.ps1` on
Windows with the same arguments.

The build uses the remote `carven-xmake-repo` by default. For joint compiler and
rule development, set `CARVEN_XMAKE_REPO_DIR` to a local rule checkout. Relative
paths resolve from the Carven repository root. In a POSIX shell:

```sh
export CARVEN_XMAKE_REPO_DIR=/path/to/carven-xmake-repo
./xmakew f -c -y
./xmakew require --force -y
```

In PowerShell, set `$env:CARVEN_XMAKE_REPO_DIR` to the checkout path. Keep the
variable set for build and test commands. Repeat configuration and package refresh
after local rule changes; reconfigure after changing the installed Crafts inventory.

Build the compiler before testing. After a successful build, select the relevant
groups or the full suite. Rebuild after code changes before rerunning tests.

| Command | Action |
| --- | --- |
| `./xmakew build` | Build the compiler |
| `./xmakew test -g <group>` | Run a selected test group |
| `./xmakew test` | Run all registered tests |
| `./xmakew build carven-format` | Build the formatter |
| `./xmakew format-check` | Check C++ and Carven formatting |
| `./xmakew format` | Apply C++ and Carven formatting |
| `./xmakew check clang.tidy` | Analyze handwritten and generated C++ |
| `./xmakew clean` | Clear build outputs |

For unexpected compiler, module, BMI, dependency-order, or apparently impossible
type errors, run `./xmakew clean` and `./xmakew build`, then reproduce before
diagnosing source code.

Build `carven-format` before checking or applying formatting. Rebuild and test after
formatting changes sources.
