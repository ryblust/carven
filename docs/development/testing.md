# Testing

This document defines test placement, evidence, and the validation workflow.
Cases specify inputs, conditions, and expected results under the current
contracts. Target configurations specify supported build and execution modes.

## Validation

Use the repository wrapper locally. Test and example targets are always
registered with `set_default(false)`, so the default build selects only the
compiler and its dependencies. `test -g <group>` builds and runs the selected
group; `test` builds and runs all registered tests, including non-default
targets.

Build or update the local compiler before testing. Generated-code tests invoke
Carven in Xmake's prepare phase, before target dependencies are built:

```shell
./xmakew build
./xmakew test -g internal
./xmakew test -g language
./xmakew test -g crafts
./xmakew test -g interop
./xmakew test -g cli
./xmakew test -g examples
```

During implementation, run the relevant groups. After implementation, run the
whole suite and static analysis:

```shell
./xmakew test
./xmakew check clang.tidy
```

Clang-tidy checks registered handwritten and generated C++ translation units,
including crafts headers they use. Generated-code findings are addressed in the
generator and verified after regeneration. Static analysis is read-only; fixes
are made in the owning source.

`.clang-tidy` applies to handwritten C++; generated C++ uses
`xmake/generated.clang-tidy`. Findings from selected checks fail the analysis
command in both profiles.

Reproduce unexpected module or dependency failures after `./xmakew clean` and
`./xmakew build`. Clean the build tree before changing toolchains or switching
between the wrapper and stock Xmake. If the wrapper cannot apply its patch,
run `xmake clean -a` and `xmake build`, then use stock Xmake for validation.

For local build-rule development, set `CARVEN_XMAKE_REPO_DIR` to the rule checkout
when building.

## Test responsibilities

| Group | Boundary | Evidence |
| --- | --- | --- |
| `internal` | Compiler modules and runtime support | Semantic rules, diagnostics, representation invariants, planning, serialization, and runtime operations |
| `language` | Compiled and executed Carven programs | Values, access, ownership, control, failure, modules, closures, and testing behavior |
| `crafts` | Public source-package APIs | Library results, errors, state transitions, and algorithms |
| `interop` | C++ providers, consumers, and support headers | Boundary signatures, source fragments, native calls, Unicode checks, and header self-containment |
| `examples` | User-facing programs | Documented program output from the actual example executables |
| `cli` | Compiler process, native/ interpreted execution, and build integration | Arguments, output, exit status, files, source scheduling, and generation policy |

Place each case in the group that owns the tested boundary. Specify accepted
behavior, rejected inputs, and representation invariants from the current contract.
Reuse fixtures and assertions across the C++ standard modes in the test matrix.
Each matrix dimension covers a distinct contract or supported execution mode.
Merge cases that repeat the same input class, execution path, and observation.

Apply the following C++ modes:

| Subject | Compiler mode |
| --- | --- |
| Compiler internals and Carven diagnostics | Compiler's own C++26 configuration |
| Language programs, entry and reporting contracts | C++20 baseline |
| Interop programs, native rejection and termination contracts | C++20 baseline |
| Crafts public APIs | C++20 baseline |
| Examples | C++20 baseline |
| Builtin printing and printing termination contracts | C++20 and C++23 implementations |
| Native C++23 print API | C++23 |

C++20 is the generated-source baseline. Additional standard modes exercise
standard-library capability branches; shared semantic contracts run at baseline.
Declare shared sources once in each group's `xmake.lua` and
apply the modes listed above. Entry tests cover default
and explicit entries, success and failure status, reported failures, and cleanup.

Execution-selection tests use valid, terminating operands and assert call counts
or execution traces. Internal static-execution tests can observe calls through
`SemanticExecutionContext`. Invalid-input and resource-limit cases separately
assert their diagnostics.

Resource-accounting cases exercise actual construction, copying, calls, and queries
with small inputs at accepted and rejected limits. Source-level cases use production
defaults to check diagnostics, source locations, and call traces. Large inputs serve
scale-dependent contracts such as stack depth or retained-storage growth.
Semantic resource-limit cases stop after analysis and publication. Backend tests
cover generation of the corresponding operations with representative inputs.
The internal runner registers static-specialization budgets separately from its
other cases, using complementary filters on the same executable. Both selections
belong to the `internal` group and run in the full suite.

Language tests use local Carven state for counters and execution traces.
A language fixture may use a same-stem C++ provider header for observations that
Carven cannot express. Tests whose subject is that C++ boundary belong in
`interop`. Internal C++ tests use the framework under `tests/harness/`;
generated programs use Carven's testing support. `const test` checks execute
during Carven compilation and do not generate
runtime test functions. Test names may be omitted; an anonymous failure reports
its file, line, and column. Explicit names retain module-local uniqueness.
`const` block labels are optional diagnostic strings and may repeat.
`carven interpret --tests` executes ordinary tests in the
interpreter subset with runtime semantics. Use shared fixtures to compare interpreted
and native behavior, including helper assertions and execution ordering.

Internal interpreter tests cover admission, failure propagation, and per-test
execution budgets. Language tests cover generated test runners and their exit
status. CLI tests cover test options, source ordering, matching native/interpreted
report layouts, and report ordering relative to source output on the same stream.
Native report scenarios use ordinary prebuilt targets and share executables when
their entry and build requirements match. Direct-run CLI cases retain coverage of
source collection, native compilation, argument forwarding, and process results.
Assertion tests cover conditional messages, fatal termination, and
independence from `NDEBUG`. Generation tests check that known conditions retain
required effects without redundant report branches or storage.

Use `const test` for standard-library algorithm boundaries and error contracts
that support static execution. Keep representative generated-program cases
for native execution: compiler evaluation and generated C++ are separate
execution boundaries. Runtime cases also cover native storage and lifetimes,
streaming state, and exhaustive domains beyond static-execution budgets.
Avoid repeating a full input table when it adds no distinct execution evidence.

User-facing programs live under `examples/`. Their output checks belong to the
`examples` group; diagnostic and termination cases belong to the test suites.

Crafts public API tests live under `tests/crafts/<craft>/`, mirroring the package
module hierarchy. The C++20 default target covers all craft APIs. A second target
forces the portable SIMD backend without instruction-set options and covers UTF-8
block and streaming validation. Both use Carven's generated default
inline-test entry. Production sources are supplied by the package rule; targets
add test sources explicitly. Application and Crafts test targets use the same
package rule.

Compiler targets and the general internal and craft tests add no SIMD
instruction-set options. SIMD algorithms, runtime representation, and memory
contracts belong to C++20 interop consumer targets. The SIMD consumer requests AVX2 through
`add_vectorexts`; Xmake maps the option for the compiler and ignores unsupported
options. This checks compiler support, not the running CPU: an x86 build that
accepts AVX2 requires an AVX2-capable test machine. A portable consumer checks the
same contracts without instruction-set options. Functional correctness does not
require enabling an extension.

## Assertions

The shared C++ runner lives under `tests/harness/` and is linked only into test
binaries through `carven-test-support`. Tests import the framework and fixture
partitions they use. Its API belongs to `carven::testing`, locally aliased as
`ct`. A file-local `Suite` registers noncapturing `noexcept` case bodies during
collection. Duplicate names, empty selections, and cases with no assertions fail.

Use `ct::each` for independent table inputs and `ct::scenario` for other scoped
input contexts. An input's name accompanies its failures; returning from an
`each` callback permits the next input to run. Empty tables fail. Scenarios are
contexts within a case, not separately selected or counted tests. Ordinary loops
serve traversal, ordering, and accumulation.

Comparison assertions report actual and expected values. Text operands compare
contents, including string literals and C strings; null C strings are distinct
from empty text. Use an explicit-length string view for bytes containing NUL.
Text and range equality reports include lengths and the first differing byte or
element. Use `ct::expect` for predicates and opaque values, and `.note(...)` for
input details or the meaning of a condition. A `noexcept` note callback computes
expensive context only on failure.

Guard premises before indexing, dereferencing, or reading an error. Assertion
results convert to Boolean so a failed premise can return from the case or its
current input. `ct::require` terminates the process; reserve it for fixture
construction that cannot produce a valid value or other unrecoverable failures.
The framework's process tests check execution, reporting, and rejected runs.

Diagnostic assertions compare typed codes and report actual findings.
`ct::find_diagnostic` borrows from the diagnostic collection; source-aware output
uses the compiler's diagnostic renderer. `ct::TempDirectory` exclusively creates
its directory and attempts removal at destruction. File-operation tests prepare
and observe bytes through independent standard file I/O. Domain fixtures own
their setup and observations.

Every registered test target must build successfully. Do not use
`build_should_fail` or `should_fail`. A test driver returns success when its
assertions pass. Compile-only targets verify compilation and linking.
Diagnostic and termination tests assert the expected diagnostic or termination
contract. Aggregate generated-test targets require a positive passing-case
summary, so an empty runner cannot pass solely by exiting successfully.

Cases cover acceptance, rejection, results, effects, and lifecycle boundaries.
Distinct source entry points need additional cases when they exercise different
paths or contracts. Internal tests check valid representations and malformed
states at the boundary that owns the invariant, including identity, range,
ownership, and structural relations.

Limit fixture setup to the inputs required by its assertions. Add cases for
distinct inputs, interactions, or observations.

Language-behavior tests identify the owning semantic section through their
case name or a focused comment. Captures and views need interaction coverage for
copying, mutation, transfer, scope exit, and calls. Runtime assertions check results;
diagnostic tests check static restrictions. Exercise compiled uses of runtime
helpers in generated-program tests.

Check generated-code behavior by compiling and executing it. Use text assertions
for serialized syntax, source attribution, raw payload preservation, and artifact
paths. Avoid assertions on temporary names, helper spellings, and complete
generated bodies.

Target structure assertions check a stated property, such as bounded node growth
or single execution of an operation. Scope cost assertions to the measured
operation. Accept equivalent representations that preserve the checked property.

Lifecycle tests observe construction, transfer, execution, and destruction in
order, including conditional paths and failure cleanup. Native construction tests
compile generated programs with the constructor capabilities relevant to the
operation, including immovable prvalues where supported. Runtime helper tests
check their own invocation and storage contracts.

Diagnostic tests compare identity, severity, and relevant source location.
Presentation tests may check diagnostic transport or wording where that is
their subject. Assert structured violations directly where the validator exposes
them. Use the internal death-test harness to check SIGABRT at terminating
boundaries. Give each input, including loop iterations and subcases, a distinct
scenario name within its test case.
Prepare and validate fixtures before entering the death-test action. The action
contains only the production operations whose termination is under test; a test
assertion inside it can itself abort and falsely satisfy the contract.

For implementation selection, identify the source fact, work removed, and
obligations preserved. Test semantic and generated-structure contracts at their
owning boundaries. Measure runtime cost and compilation time separately;
code size alone establishes neither. Keep measurement logs outside reference
documents.

`./xmakew bench compile` measures source-to-C++ compilation for module batches
and structured workloads. `./xmakew bench incremental` measures build times and
C++ object rebuild counts in a small module dependency fixture. Both update the
configured compiler before measuring. See [Benchmarks](../../xmake/README.md#benchmarks)
for workloads, measurement boundaries, and sampling options.

## Organization

Each group owns its `xmake.lua`. Cases follow the repository directory and C++
conventions. Topic directories name the rule being tested; individual
features and scenarios belong in filenames and case names. Language tests group
type operations under `types`, text under `text`, and functions and callable views
under `functions`. Interop `bindings` covers C++ declaration lookup and use;
`lifetimes` covers native construction, transfer, and cleanup. The internal harness owns process-based invariant termination.
Process harnesses bound execution time. The CLI harness records stdout and
stderr for each step, preserves failed fixtures, and removes successful temporary
directories. CLI scenario tables reject unknown fields, ignored top-level step
fields, and workflows with no executable steps; failure reports identify the
step and command.

Fixtures whose exact source bytes are part of the assertion use `.cv.fixture`
and the CLI `fixtures` mapping to copy them to a `.cv` input. This keeps source
locations and verbatim excerpts stable under repository formatting. Behavioral
tests use ordinary `.cv` sources and avoid fixed line numbers unless source
location is the contract under test.
Prebuilt CLI report fixtures use `build/cli_fixtures/` for stable source identities.
Preparation preserves unchanged file contents and timestamps; generated C++ and
objects use the configured build directory. Report comparisons remove the known
fixture prefix, while failure logs retain the original output.

Generated target configuration directly expresses the boundary under test.
Use ordinary targets and native Xmake test assertions for program execution and
output checks. Custom harnesses assert process, diagnostic, or artifact contracts
that need additional observations.
Place new cases in the owning domain and extend an existing target when its
build and execution requirements fit. Separate targets express incompatible
entry definitions, compiler settings, or linkage requirements. Helpers stay
within their owning domain; scenario selection stays within the target harness.
Run terminating checks in isolated processes with a specific expected outcome.
Register generated-program termination and entry scenarios as separate Xmake
tests. Share the executable when their build requirements match.

Native compilation rejection fixtures live under `tests/interop/rejections/`.
Each fixture is registered independently with an expected diagnostic, subject,
and source attribution, either directly or through a configured template note.
The shared compile harness requires Carven generation to succeed, then checks
native compiler exit status `1` and the expected diagnostic. It retains artifacts
on failure. Accepted counterparts belong to the ordinary interop sources and are
checked by the normal build. Rejection fixtures compile on each test run.

Cases initialize their own observable state. Temporary directories and captured
streams use fresh paths for each invocation, including concurrent suite runs.
Borrows in fixtures obey their owner lifetimes.

Header self-containment checks use one translation unit per header, including
generated interfaces, within the consumer target. These units instantiate
interfaces; behavioral assertions belong in the corresponding runtime or
generated-program tests.

Test runtime exception boundaries in isolated C++ consumer processes. Require the
throwing operation to execute and reach the installed termination handler. Catch
exceptions outside the runtime call and report escaped exceptions as test failures.

CLI execution cases cover the shared top-level language surface, analysis-time
output, `const` blocks and static tests, native argument forwarding, interpreter
admission, source traces, runtime arithmetic, and resource failures. Interpreter
acceptance uses expected program results; compiled execution also exercises generated C++.

## Graver

`./xmakew test -g graver` runs C++ tests for source preservation, layout,
formatting, batch results, and file replacement. Formatting fixtures check exact
output, idempotence, and output stability after horizontal-whitespace changes.
Invalid inputs check lexical and syntax errors. The separately registered corpus
test checks formatting, output parsing, and idempotence for repository programs.
Both registrations use the shared internal runner: the ordinary run excludes
`Graver corpus:*`, and the corpus run selects that pattern. The runner also
supports `--test "Area: behavior"` for one exact case and `--list-tests` for
inspection. Empty selections fail.

Xmake registers each CLI scenario separately for selection and reporting. The
CLI harness checks exit codes, stdout, stderr, and filesystem changes. Formatting
fixtures under `tools/graver/tests/format/` pair `input.cv` with `expected.cv`.
