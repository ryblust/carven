# Testing

This document defines test placement, evidence, and validation. Test current
contracts: accepted behavior, rejected inputs, and invariants at their owning
boundaries. Add cases for missing evidence. Accept equivalent implementations
that preserve the tested property. Product suites exercise shared test
infrastructure. Additional infrastructure coverage addresses contracts those
suites do not establish.

## Test responsibilities

| Group | Boundary | Evidence |
| --- | --- | --- |
| `internal` | Compiler modules and runtime support | Semantic rules, diagnostics, representation invariants, planning, serialization, and runtime operations |
| `language` | Compiled and executed Carven programs | Values, access, ownership, control, failure, modules, closures, and testing behavior |
| `crafts` | Public source-package APIs | Library results, errors, state transitions, and algorithms |
| `interop` | C++ providers, consumers, and support headers | Boundary signatures, native calls, source fragments, and header self-containment |
| `examples` | User-facing programs | Documented output from actual example executables |
| `cli` | Compiler processes, interpreted execution, and build integration | Arguments, reports, exit status, files, scheduling, and generation policy |
| `graver` | Source formatting and file operations | Source preservation, layout, errors, batch results, and replacement |
| `editor` | Document snapshots and cached source queries | Source observations, result ownership, versions, and invalidation |
| `analyzer` | Resident analysis sessions and process messages | Atomic requests, owning responses, message encoding, transport, and process lifetime |

Place each case in the group that owns the tested boundary. Reuse fixtures and
assertions across applicable configurations. Merge cases that repeat the same
input class, execution path, and observation.

Compiler internals use the compiler's C++ configuration. Generated-source and
consumer contracts run at the C++20 baseline. Additional standard modes exercise
native capability branches; they do not require repeating shared semantic
contracts. Declare shared sources once and select modes for the evidence they add.

Static execution, interpreted execution, and generated native C++ are separate
boundaries. Keep representative cases for each applicable boundary; avoid
repeating a full input table without distinct evidence. Static tests establish
compile-time contracts; native tests also observe storage, lifetimes, and runtime
state. Interpreter tests establish admission, failure propagation, and execution
budgets. CLI tests establish process options, source ordering, report transport,
and ordering relative to program output.

Language tests use local Carven state for counters and traces. A native provider
may expose observations Carven cannot express; cases whose subject is that native
boundary belong in `interop`. User-facing programs live under `examples/`;
diagnostic and termination cases belong to test suites.

Crafts tests mirror the package's module ownership and use the application package
rule. Portable and accelerated implementations establish the same functional
contracts. Instruction-set options belong to consumer targets that test those
implementations; compiler support for an extension does not establish machine
support. Functional correctness must also hold without enabling an extension.

## Assertions

Internal C++ tests use the shared harness in `tests/harness/` and import only
the framework and domain fixtures they need. Generated programs use Carven's
testing support. Follow neighboring suites for registration and assertion syntax.

Test names identify the owning component and observed behavior. Give independent
inputs distinct names and retain their identity in failure context. Scoped inputs
remain observations within a case; ordinary traversal and accumulation do not
require separate scenarios. An empty case table provides no evidence for its
contract; empty values remain valid inputs where the product contract permits.

Comparison assertions report actual and expected values. Use predicates for
opaque values and attach context that explains the input or condition. Text
assertions compare contents; binary data retains explicit lengths. Compute
expensive failure context only when needed.

Guard premises before indexing, dereferencing, or reading an error. When a premise
assertion fails, explicitly return from the current case or input before dependent
observations. Nonfatal assertions record failure and continue execution. In the
internal C++ harness, a failed `require` terminates the process; reserve it for
unrecoverable fixture construction.

Every registered target must build successfully. A test driver returns success
when its assertions pass, including expected rejection or termination. Compile-only
targets verify compilation and linking. Generated test runners require evidence
that cases passed; successful process exit alone cannot establish a nonempty run.

Internal tests check valid representations and malformed states at the boundary
that owns the invariant, including identity, range, ownership, and structural
relations. Assert structured violations directly when the validator exposes them.
Diagnostic tests compare identity, severity, and relevant source attribution.
Wording and transport are contracts when presentation is the subject.

Check generated-code behavior by compiling and executing it. Exact text belongs
to serialization, source attribution, raw payload preservation, artifact paths,
and other byte-level contracts. Avoid assertions on temporary names, helper
spellings, or complete generated bodies.

Structure assertions state a property such as bounded growth or single execution.
Scope cost assertions to the measured operation and state the bound each scale
test checks. Cover relevant dimensions rather than one preferred representation.
Generation simplifications preserve required effects while removing redundant
conditions, state, or intermediate work; assertions establish those properties
without fixing the complete emitted form.

Execution-selection tests use valid, terminating operands and observe call counts
or execution traces. Invalid-input and resource-limit tests separately establish
their diagnostics. Resource accounting exercises actual construction, copying,
calls, and queries with small inputs at accepted and rejected limits. Source-level
cases use production defaults to check diagnostics, source locations, and traces.
Large inputs serve a stated scale-dependent property. Semantic limit tests stop at
analysis and publication; backend tests establish generation of representative
operations. Independent expensive contracts have separate results and execution
bounds so unrelated costs do not accumulate into one timeout.

Lifecycle tests observe construction, transfer, execution, and destruction in
order, including conditional paths and failure cleanup. Captures and views need
interaction coverage for copying, mutation, transfer, scope exit, and calls.
Runtime assertions establish results; diagnostic tests establish static
restrictions. Generated-program tests exercise compiled uses of runtime helpers,
including the native constructor capabilities relevant to the operation.

For optimization changes, identify the work removed and establish affected
semantic and structural contracts at their owning boundaries. Compare workloads
under equal results, effects, and output contracts. Select measurement dimensions
from the affected work and performance claims. Compiler time and memory,
generated-code size, native compilation, and runtime work are distinct costs;
measure selected dimensions separately. Limit conclusions to measured dimensions
and keep logs outside reference documents. Define measurement boundaries,
workloads, and sampling conditions before comparing results; retain the same
conditions across implementations.

## Organization and fixtures

Each group owns its `xmake.lua`. Topic directories name the rule under test;
features and scenarios belong in filenames and case names. Keep contract, state,
and implementation within their owning responsibility. Helpers remain with their
owning domain; scenario selection remains within the target harness.

Extend an existing target when its build and execution requirements fit. Separate
targets express different entry definitions, compiler settings, or linkage
requirements. Share executables when those requirements match. Use ordinary
Xmake targets and native test assertions for execution and output checks; custom
harnesses establish process, diagnostic, or artifact contracts needing additional
observations. Generated target configuration directly expresses the boundary
under test.

Cases initialize their own observable state. Temporary directories and captured
streams use fresh paths for each invocation, including concurrent runs. Borrows
obey their owner lifetimes. Domain fixtures own setup and observations;
file-operation tests prepare and observe bytes through independent file I/O.
Process harnesses bound execution time, retain failed artifacts, and clean up
successful temporary state.

Fixtures with significant source bytes use `.cv.fixture` inputs copied to source
files by their harness. Behavioral tests use ordinary sources and avoid fixed
line numbers unless source attribution is the contract. Formatting fixtures check
source preservation, expected output, idempotence, and stability under irrelevant
whitespace changes. Corpus tests establish formatting, parsing, and idempotence
for repository programs.

Native compilation rejection fixtures establish successful Carven generation
before observing the native rejection. Each rejection has its own expected
diagnostic, subject, and source attribution. Accepted counterparts use the normal
interop build. Rejections compile on each test run and retain artifacts on failure.

Header self-containment checks use one translation unit per header within its
consumer target and instantiate its interface. Behavioral observations belong in
runtime or generated-program tests.

Terminating operations run in isolated processes with a specific expected outcome.
Validate setup before entering a death-test action; the action contains only the
production operations under test. Assertions inside it can abort and falsely
satisfy the observation.

Native exception-boundary tests require the throwing operation to execute and
reach the installed termination handler. Observe escaped exceptions outside the
runtime call as failures. Allocation-failure injection uses separate executables
so other exception tests retain the configured runtime and allocator. Injection
must reach the allocations under test, including allocations across library
linkage boundaries.

## Validation

Use `./xmakew`. Test and example targets are registered as non-default targets;
selected-group tests build and run that group, and the full test command includes
all registered tests. Build or update the compiler first: generated-test
preparation invokes it before target dependencies are built.

During implementation, run the owning groups with `./xmakew test -g <group>`.
Before publishing, format, run the complete suite, and perform static analysis:

```shell
./xmakew build
./xmakew build graver
./xmakew format
./xmakew test
./xmakew check clang.tidy
```

Static analysis checks handwritten and generated C++ translation units and the
support headers they use. Fix findings in the owning source and verify generated
changes after regeneration. Static analysis is read-only. `.clang-tidy` owns the
handwritten profile; `xmake/generated.clang-tidy` owns the generated profile.

Reproduce unexpected compiler, module, or dependency failures after a clean
rebuild. Clean before changing toolchains, instrumentation, or build drivers. If
the wrapper cannot apply its patch, use stock Xmake after cleaning and rebuilding.
Local build-rule development can select its checkout through
`CARVEN_XMAKE_REPO_DIR`.

CI runs the platform, sanitizer, and static-analysis matrix defined in
`.github/workflows/ci.yml`. Instrumentation must also reach native compilation
launched outside build targets. Build and harness configuration own modes,
selections, and timeouts.
