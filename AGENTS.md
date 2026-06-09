# AGENTS.md

This file provides guidance to coding agents working with this repository.

## Overview

Carven is a programming language that transpiles `.cv` source files to standard C++ code. The transpiler itself is written in C++26 using modules (`.cppm` files) and builds with xmake.

Full language grammar and supported features are documented in [`docs/grammar.md`](docs/grammar.md).

## Build & Run

```shell
xmake f --toolchain=clang-cl # Windows
xmake f --toolchain=llvm # macOS

# Use after encountering BMI/cache issues, strange module errors, or
# unexplained undefined symbols after changing exported module APIs.
# Prefer this before diagnosing source-level regressions.
rm -rf build .xmake
xmake f --toolchain=clang-cl # Windows
xmake f --toolchain=llvm # macOS
xmake build

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

## Key Design Decisions

- **Span lifecycle**: AST nodes store `Span` (byte offsets), not owned strings. Codegen extracts text via `slice(source, span)`. The source text MUST outlive all AST references.
- **Xmake-backed builds**: xmake is the build source of truth. Project builds use the nearest `xmake.lua`; `.cv` files are compiled through `xmake/rules/carven.lua`. Do not reintroduce direct compiler invocation in normal `carven run` / `carven build` paths.
- **Single-file cache projects**: `carven run <file.cv>` creates an internal xmake project under the platform cache directory at `carven/scripts/<stable-id>`. Do not write temporary script projects into the user's source tree.
- **Project argument forwarding**: `carven run <target> -- <args...>` delegates to `xmake run <target> <args...>`. Runtime args in project mode require an explicit target because xmake parses leading `-` arguments as xmake options when no target is present.
- **Import lowering**: most `import` statements pass through as C++ module imports. Current `import std;` is a temporary header-preamble fallback; true standard-library module support is tracked in `docs/proposals/true-import-std.md`.
- **Arena allocator**: AST nodes allocated via bump-pointer `Arena`. Use `alloc<T>(args...)` — never `new` for AST nodes.
- **Context-free parser**: Parser distinguishes constructs by token type only, never by symbol table lookup. Semantic checks live in `sema`; lowering decisions live in codegen.
- **Monadic error handling**: `std::optional<T>` and `std::expected<T, E>`. No exceptions (`-fno-exceptions`). Every function `noexcept`.
- **Test split**: `carven-unit-test` is the fast language/tooling suite and must not run xmake subprocesses. `carven-e2e-test` owns compiled xmake smoke coverage. Do not recreate the old combined `carven-test` target.
- **Golden files**: generated C++ snapshots live in `tests/golden/`. Update them only with `scripts/update-golden.sh`, then review the diff.
- **Install layout**: `xmake install -o <prefix> carven` installs the CLI to `<prefix>/bin` and rule files to `<prefix>/share/carven/xmake/rules`. `scripts/install.sh` is only a small wrapper around build/install and must not edit environment files.

## Coding Style

### Naming
- Functions: `snake_case`
- Types/Enums/Enum values: `PascalCase`
- Fields: `snake_case`
- Modules: `carven.<layer>.<name>`

C++ conventions are defined in two specs under `.agents/specs/`:

- [`cpp-design`](.agents/specs/cpp-design.md) — design philosophy and API patterns
- [`cpp-format`](.agents/specs/cpp-format.md) — formatting rules

All agents must apply both when writing, modifying, or reviewing C++ code.

Feature design proposals live in [`docs/proposals/`](docs/proposals/). When a proposal is fully implemented, delete it.

## Conventional Commits

Format: `<type>(<scope>, ...): <description>`
- Types: `feat`, `fix`, `refactor`, `chore`, `docs`, `ci`, `test`
- Scopes: match `src/` subdirectories or feature names
