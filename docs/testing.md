# Testing

This document defines test placement, evidence, and the validation workflow.
Cases specify inputs, conditions, and expected results. Target configurations
specify build and execution modes.

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

The [base configuration](../.clang-tidy) applies to handwritten C++; generated
C++ uses [its derived configuration](../xmake/generated.clang-tidy). Findings
from selected checks fail the analysis command in both profiles.

Reproduce unexpected module or dependency failures after `./xmakew clean` and
`./xmakew build`. Clean the build tree before changing toolchains or switching
between the wrapper and stock Xmake. If the wrapper cannot apply its patch,
run `xmake clean -a` and `xmake build`, then use stock Xmake for validation.

For local build-rule development, set `CARVEN_XMAKE_REPO_DIR` to the rule checkout
when building.

## Test responsibilities

| Group | Boundary | Evidence |
| --- | --- | --- |
| `internal` | Compiler modules and runtime facilities | Semantic rules, diagnostics, representation invariants, planning, serialization, and runtime operations |
| `language` | Compiled and executed Carven programs | Values, access, ownership, control, failure, modules, closures, and testing behavior |
| `interop` | C++ providers, consumers, and support headers | Boundary signatures, source fragments, native calls, Unicode checks, and header self-containment |
| `examples` | User-facing programs | Documented program output from the actual example executables |
| `cli` | Compiler process and build integration | Arguments, output, exit status, files, source scheduling, and generation policy |

Place each case in the group that owns the tested boundary. Reuse fixtures and
assertions across supported build modes.

Apply the following C++ modes:

| Subject | Compiler mode |
| --- | --- |
| Compiler internals and Carven diagnostics | Compiler's own C++26 configuration |
| Language programs, entry and reporting contracts | C++20 and C++23 |
| Interop programs, native rejection and termination contracts | C++20 and C++23 |
| Examples | C++20 baseline |
| C++23 print | C++23 |

C++20 is the generated-source baseline. Declare shared sources once in each
group's `xmake.lua` and apply the modes listed above. Entry tests cover default
and explicit entries, success and failure status, reported failures, and cleanup.

A language fixture may use a same-stem C++ provider header for observations that
Carven cannot express. Tests whose subject is that C++ boundary belong in
`interop`. Internal tests use doctest; generated programs use Carven's testing
support.

User-facing programs live under `examples/`. Their output checks belong to the
`examples` group; diagnostic and termination cases belong to the test suites.

## Assertions

Every registered test target must build successfully. Do not use
`build_should_fail` or `should_fail`. A test driver returns success when its
assertions pass. Compile-only targets verify compilation and linking.
Diagnostic and termination tests assert the expected diagnostic or termination
contract.

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

Runtime cost and compilation time are measured separately. The manual workload
in `xmake/build_pulse.lua` measures fresh build throughput, module scaling,
and private-edit locality. `xmake/analysis_pulse.lua` measures call-chain
ordering and structured loop depth. Their contracts are in `xmake/benchmarks.md`.

## Organization

Each group owns its `xmake.lua`. Cases follow the repository directory and C++
conventions. The internal harness owns process-based invariant termination.
Process harnesses bound execution time. The CLI harness records stdout and
stderr for each step, preserves failed fixtures, and removes successful temporary
directories.

Generated target configuration directly expresses the boundary under test.
Place new cases in the owning domain and extend an existing target when its
build and execution requirements fit. Separate targets express incompatible
entry definitions, compiler settings, or linkage requirements. Helpers stay
within their owning domain; scenario selection stays within the target harness.
Run terminating checks in isolated processes with a specific expected outcome.
Register generated-program termination and entry scenarios as separate Xmake
tests. Share the executable when their build requirements match.

Native compilation rejection fixtures live under `tests/interop/rejections/`.
Each fixture is registered independently with an expected primary diagnostic,
subject, and source or generated-header attribution. The shared compile harness
requires Carven generation to succeed, then checks native compiler exit status
`1` and the expected diagnostic. It retains artifacts on failure. Accepted
counterparts belong to the ordinary interop sources and are checked by the
normal build. Rejection fixtures compile on each test run.

Cases initialize their own observable state. Temporary directories and captured
streams use fresh paths for each invocation, including concurrent suite runs.
Borrows in fixtures obey their owner lifetimes.

Header self-containment checks use one translation unit per header, including
generated interfaces, within the consumer target. These units instantiate
interfaces; behavioral assertions belong in the corresponding runtime or
generated-program tests.

Runtime exception boundaries are tested in isolated C++ consumer processes.
These checks require the throwing operation to execute and reach the installed
termination handler, establishing the runtime's `noexcept` contract.
