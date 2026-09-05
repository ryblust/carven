# Testing

This document owns test placement, assertions, and the validation workflow.

## Validation

Use the repository wrapper. Build before testing:

```shell
./xmakew build
./xmakew test -g internal
./xmakew test -g language
./xmakew test -g interop
./xmakew test -g cli
```

During implementation, run the relevant groups. After implementation, run the
whole suite and static analysis:

```shell
./xmakew test
./xmakew check clang.tidy
```

Clang-tidy checks registered compiler, test, and generated C++ translation units,
including crafts headers they use. Unexpected module or dependency failures
must be reproduced after `./xmakew clean` and `./xmakew build`.

For local build-rule development, set `CARVEN_XMAKE_REPO_DIR` to the rule checkout
when building. Stock Xmake is the fallback only when the wrapper cannot apply
its versioned patch.

## Test responsibilities

| Group | Boundary | Evidence |
| --- | --- | --- |
| `internal` | Compiler modules and runtime facilities | Semantic rules, diagnostics, representation invariants, planning, serialization, and runtime operations |
| `language` | Compiled and executed Carven programs | Values, access, ownership, control, failure, modules, closures, and testing behavior |
| `interop` | C++ providers, consumers, and support headers | Boundary signatures, source fragments, native calls, Unicode checks, and header self-containment |
| `cli` | Compiler process and build integration | Arguments, output, exit status, files, source scheduling, and generation policy |

Generated programs use C++20. The language corpus runs once in that target mode.
Default and caller-provided test entries use the same generated runner. C++
provider and consumer cases execute against the current boundary contract.
Support headers are also compiled individually to check self-containment.

A language fixture may use a same-stem C++ provider header for observations that
Carven cannot express. Tests whose subject is that C++ boundary belong in
`interop`. Internal tests use doctest; generated programs use Carven's testing
support.

## Assertions

Each test establishes valid behavior, rejection of an invalid program, or an
invariant of a representation. Place it at the boundary that owns the fact.
Retain multiple tests only when they establish distinct contracts.

Language-behavior tests identify the owning semantic section through their
case name or a focused comment. Cover acceptance, rejection, and relevant
boundaries. Captures and views also need interaction coverage for copying,
mutation, transfer, scope exit, and calls. Runtime assertions establish results;
diagnostic tests establish static restrictions. Runtime helper tests do not
alone establish that compiled source uses those helpers correctly.

Generated-code correctness is checked by compiling and executing it. Text
assertions are appropriate for serialized syntax, source attribution, raw
payload preservation, and artifact paths. Temporary names, helper spellings,
old representations, and complete generated bodies are not contracts.

Diagnostic tests compare identity, severity, and relevant source location.
Presentation tests may check diagnostic transport or wording where that is
their subject. Malformed compiler representations use the internal death-test
harness and an explicit invariant scenario.

Tests describe the current contract. When observed behavior has no corresponding
rule, resolve the specification gap before treating that behavior as required.
Examples supplement normative rules rather than replacing them.

Runtime cost and compilation time are measured separately. The manual workload
in `benchmarks/build_pulse.py` measures fresh build throughput, module scaling,
and private-edit locality; its contract is in `benchmarks/README.md`.

## Organization

Each group owns its `xmake.lua`. Cases follow the repository directory and C++
conventions. The internal harness owns process-based invariant termination.
The CLI harness limits each process to 30 seconds and preserves failed fixtures.
Generated target configuration directly expresses the boundary under test.

Runtime exception boundaries are tested in isolated C++ consumer processes.
The throwing operation itself must execute, and the process must reach the
installed termination handler. These tests check the runtime's `noexcept`
contract without introducing C++ exceptions into Carven language fixtures.
