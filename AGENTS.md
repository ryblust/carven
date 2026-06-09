# AGENTS.md

Follow these repository rules.

## Overview

Carven transpiles `.cv` files to standard C++. The transpiler is C++26 with
modules (`.cppm`) and builds with xmake.

Grammar reference: [`docs/grammar.md`](docs/grammar.md).

## Context Loading

- Start with `rg` and the smallest relevant source files.
- Do not preload broad docs.
- Do not read `.agents/specs/` for ordinary tasks.
- Read `.agents/specs/` only for explicit `spec-review`, spec compliance
  review, or pre-commit spec pass.
- Do not read `docs/grammar.md` unless the task touches syntax, parsing,
  semantics, lowering, generated C++ output, or language feature support.
- Treat ordinary `review` as code review, not `spec-review`.

## Build & Run

```shell
xmake f --toolchain=clang-cl # Windows
xmake f --toolchain=llvm # macOS

# Build the project
xmake build

# Run fast unit tests
xmake run carven-unit-test --no-colors

# Run compiled smoke tests
xmake run carven-e2e-test --no-colors

# Run a .cv file
xmake run carven run <file.cv>

# Transpile only
xmake run carven transpile <file.cv>
xmake run carven transpile -o out.cpp <file.cv>

# Install locally without editing shell profiles or PATH
./scripts/install.sh

# Dump tokens and AST
xmake run carven dump <file>

```

## Recovery

- Use cleanup only for likely C++ module/BMI/cache problems.
- Do not use cleanup as the first step for ordinary build or test failures.
- Symptoms: strange module errors, stale BMI/cache state, or unexplained
  undefined symbols after exported module API changes.
- Before destructive cleanup, state the reason.
- Cleanup sequence: remove `build` and `.xmake`, reconfigure, rebuild.

## Development Rules

- Keep normal build, run, install, generated-file, and C++ module/BMI work
  xmake-backed. Do not add direct compiler orchestration to driver paths.
- Store spans in AST nodes. Do not store owned source strings in AST nodes.
  Source text must outlive AST references.
- Allocate AST nodes with `Arena::alloc<T>(args...)`. Do not use `new` for AST
  nodes.
- Keep parsing context-free. Do not use symbol table lookup in the parser.
  Put semantic checks in `sema`; put lowering decisions in codegen.
- Keep `carven run <file.cv>` cache projects under the platform cache
  directory. Do not write temporary script projects into the user's source tree.
- Preserve project argument forwarding: `carven run <target> -- <args...>`
  delegates to `xmake run <target> <args...>`.
- Treat `import std;` as a temporary header-preamble fallback. Do not implement
  true standard-library module support unless working on that proposal.
- Use `std::optional` / `std::expected` for failures. Do not add exceptions.
  Project functions must be `noexcept`.
- Do not make `scripts/install.sh` edit shell profiles, mutate `PATH`, or own
  upgrades/uninstalls.

## Review Behavior

- Ordinary `review` = code review.
- Report first: correctness bugs, regressions, architecture risks, missing
  tests.
- For ordinary review, do not load `.agents/specs/`.
- For ordinary review, do not run `spec-review`.
- `spec-review` = final pre-commit compliance pass after correctness and
  architecture checks.

## Test Selection

- Parser, sema, codegen, diagnostics, CLI logic: run
  `xmake run carven-unit-test --no-colors`.
- Driver, project mode, xmake integration, install layout, compiled programs, or
  cache-project behavior: also run `xmake run carven-e2e-test --no-colors`.
- Unit tests must not run xmake subprocesses.
- Compiled smoke coverage belongs in `carven-e2e-test`.
- Do not recreate the old combined `carven-test` target.
- Update generated C++ golden files only with `scripts/update-golden.sh`.

## Coding Style

### Naming
- Functions: `snake_case`
- Types/Enums/Enum values: `PascalCase`
- Fields: `snake_case`
- Modules: `carven.<layer>.<name>`

C++ specs:

- [`cpp-design`](.agents/specs/cpp-design.md) — design philosophy and API patterns
- [`cpp-format`](.agents/specs/cpp-format.md) — formatting rules

When writing or modifying C++ code:

- Follow existing local style.
- Follow the specs where relevant.
- Load full specs only under `Context Loading` rules.

Feature proposals: [`docs/proposals/`](docs/proposals/). Delete a proposal only
when the current task fully implements it and it is no longer needed.

## Conventional Commits

Format: `<type>(<scope>, ...): <description>`
- Types: `feat`, `fix`, `refactor`, `chore`, `docs`, `ci`, `test`
- Scopes: match `src/` subdirectories or feature names
