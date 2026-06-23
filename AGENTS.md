# Repository Guidelines

## Project Structure

Carven transpiles `.cv` files to C++ with xmake. Main code lives in `src/`
(`frontend`, `backend`, `driver`, `common`). Unit tests live in `tests/units`
and reviewed transpilation cases live in `tests/cases`. Project docs and
proposals live in `docs/`; build rules live in `xmake.lua` and `xmake/rules/`.

## Build & Testing

- `xmake f --toolchain=llvm`: configure a macOS/Linux LLVM build.
- `xmake f --toolchain=clang-cl`: configure a Windows clang-cl build.
- `xmake build`: build the default targets.
- `python3 tests/test.py`: run unit and e2e tests.
- `python3 tests/test.py --unit`: run unit tests.
- `python3 tests/test.py --e2e`: run installed CLI and case-based e2e checks.
- `python3 tests/test.py --list-e2e`: list e2e cases.
- `python3 tests/test.py --case <name>`: run one e2e case.
- `python3 tests/test.py --e2e --trace-commands`: print e2e subprocess timings.

Multiple e2e cases should run in a single runner, for example
`python3 tests/test.py --case transpile --case single_file_run`. Do not launch
multiple e2e runner processes in parallel because they share
`tests/e2e/.sandbox` and can delete each other's install or case workspace.

When compilation fails with an unexpected error, suspect a stale BMI cache — delete `build/` and rebuild first.

## Context Boundaries

- Do not preload broad docs for ordinary tasks.
- Do not read `.agents/specs/` unless the task is an explicit `spec-review`,
  spec compliance review, or pre-commit spec pass.
- Do not read `docs/grammar.md` unless the task touches syntax, parsing,
  semantics, lowering, generated C++ output, or language feature support.
- Treat ordinary `review` as code review, not `spec-review`.

## Engineering References

- Read `docs/implementation.md` when changing module boundaries, public APIs,
  driver/core layering, runtime integration, or test strategy.
- Read `docs/grammar.md` only when changing syntax, parsing, semantics,
  lowering, generated C++ output, or language feature support.

## Commits

Use Conventional Commits: `<type>(<scope>, ...): <description>`. Common types
include `feat`, `fix`, `refactor`, `chore`, `docs`, `ci`, and `test`.
Scopes should match `src/` subdirectories or feature names.
