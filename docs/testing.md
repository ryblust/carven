# Testing

This document defines test placement, evidence, and the validation workflow.
It covers repository validation. Individual cases and target configurations record
the scenarios and compiler modes they exercise.

## Validation

Use the repository wrapper. Build before testing:

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

Unexpected module or dependency failures must be reproduced after
`./xmakew clean` and `./xmakew build`.
After changing runtime headers, clean before rebuilding; incremental builds
currently omit some of these dependencies.

For local build-rule development, set `CARVEN_XMAKE_REPO_DIR` to the rule checkout
when building. Stock Xmake is the local fallback when the wrapper cannot apply
its versioned patch.

## Test responsibilities

| Group | Boundary | Evidence |
| --- | --- | --- |
| `internal` | Compiler modules and runtime facilities | Semantic rules, diagnostics, representation invariants, planning, serialization, and runtime operations |
| `language` | Compiled and executed Carven programs | Values, access, ownership, control, failure, modules, closures, and testing behavior |
| `interop` | C++ providers, consumers, and support headers | Boundary signatures, source fragments, native calls, Unicode checks, and header self-containment |
| `examples` | User-facing programs | Documented program output from the actual example executables |
| `cli` | Compiler process and build integration | Arguments, output, exit status, files, source scheduling, and generation policy |

Generated programs use C++20 as their baseline. The language and interop
corpora compile and execute in C++20 and C++23 with the same entry mode within
each corpus. The C++23 print fixture has its own output test target. Entry and reporting tests cover default and explicit
entries, success and failure status, reported failures, and cleanup. The group
`xmake.lua` files define the executable and compile-only targets.

A language fixture may use a same-stem C++ provider header for observations that
Carven cannot express. Tests whose subject is that C++ boundary belong in
`interop`. Internal tests use doctest; generated programs use Carven's testing
support.

User-facing programs live under `examples/` and share `examples/xmake.lua`.
Build them with `./xmakew build examples` after building the compiler. Their
output checks are included in the full suite; diagnostic and termination
coverage stays in the test groups above.

## Assertions

Every registered test target must build successfully. Do not use
`build_should_fail` or `should_fail`: a test driver succeeds only when its
assertions establish the expected behavior. Compile-only targets establish
success through compilation and linking. Diagnostic and termination tests retain
explicit assertions about the expected diagnostic or termination contract;
arbitrary failure is not sufficient evidence.

Give each rule an owning test domain. Cases cover acceptance, rejection, results,
effects, and lifecycle boundaries. Distinct source entry points need additional
cases when they exercise different paths or contracts. Internal tests check valid
representations and malformed states at the boundary that owns the invariant,
including identity, range, ownership, and structural relations.

Language-behavior tests identify the owning semantic section through their
case name or a focused comment. Captures and views need interaction coverage for
copying, mutation, transfer, scope exit, and calls. Runtime assertions establish results;
diagnostic tests establish static restrictions. Runtime helper tests do not
alone establish that compiled source uses those helpers correctly.

Generated-code correctness is checked by compiling and executing it. Text
assertions are appropriate for serialized syntax, source attribution, raw
payload preservation, and artifact paths. Temporary names, helper spellings,
and complete generated bodies are not contracts.

Generation-quality tests inspect target structure for a stated property, such as
bounded node growth or absence of duplicated execution. Scope a cost assertion to
the operation it measures. Equivalent representations are acceptable when they
preserve the property.

Lifecycle tests observe construction, transfer, execution, and destruction in
order, including conditional paths and failure cleanup. Native construction tests
compile generated programs with the constructor capabilities relevant to the
operation, including immovable prvalues where supported. Runtime helper tests
check their own invocation and storage contracts.

Diagnostic tests compare identity, severity, and relevant source location.
Presentation tests may check diagnostic transport or wording where that is
their subject. Malformed compiler representations use the internal death-test
harness and an explicit invariant scenario.

Runtime cost and compilation time are measured separately. The manual workload
in `xmake/build_pulse.lua` measures fresh build throughput, module scaling,
and private-edit locality. `xmake/analysis_pulse.lua` measures call-chain
ordering and structured loop depth. Their contracts are in `xmake/benchmarks.md`.

## Organization

Each group owns its `xmake.lua`. Cases follow the repository directory and C++
conventions. The internal harness owns process-based invariant termination.
Process harnesses bound execution time and retain failed-case output for diagnosis.
The CLI harness records stdout and stderr for each step, preserves failed
fixtures, and removes successful temporary directories.
Generated target configuration directly expresses the boundary under test.
Place new cases in the owning domain and extend an existing target when its
build and execution requirements fit. Separate targets express incompatible
entry definitions, compiler settings, or linkage requirements. Helpers stay
within their owning domain; scenario selection stays within the target harness.
Terminating checks run in isolated processes with a specific expected outcome.
A harness can select scenarios within one executable; isolation alone does not
require separate build targets. Register independent termination and entry
scenarios as separate tests sharing the same executable.
Native compilation rejection fixtures live under `tests/interop/rejections/`.
Each fixture is registered independently on `carven-test-interop-rejections`
with an expected primary diagnostic, subject, and source or generated-header
attribution. The shared compile harness
requires Carven generation to succeed, then checks the native compiler rejection;
it retains artifacts on failure. Accepted counterparts belong to the ordinary
interop sources and are checked by the normal build. Behavior tests do not
compile diagnostic fixtures. Independent rejection tests can run concurrently
through the test scheduler; they intentionally recompile on each test run.
Cases initialize their own observable state. Temporary directories and captured
streams use fresh paths for each invocation, including concurrent suite runs.
Borrows in fixtures obey their owner lifetimes. Header self-containment checks use
one translation unit per header, including generated interfaces, within the
consumer target. These units instantiate interfaces; behavioral assertions belong
in the corresponding runtime or generated-program tests.

Runtime exception boundaries are tested in isolated C++ consumer processes.
The throwing operation itself must execute, and the process must reach the
installed termination handler. These tests check the runtime's `noexcept`
contract without introducing C++ exceptions into Carven language fixtures.
