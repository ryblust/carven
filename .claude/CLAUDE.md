# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

Zero is a programming language that transpiles `.zero` source files to standard C++ code. The transpiler itself is written in C++26 using modules (`.cppm` files) and builds with xmake.

Full language grammar and supported features are documented in [`docs/grammar.md`](docs/grammar.md).

## Build & Run

```shell
# Configure (Windows, first time or after clean)
xmake f --toolchain=clang-cl

# Configure (macOS) — adjust LLVM path to match installed version
xmake f --toolchain=llvm --sdk=/opt/homebrew/Cellar/llvm/22.1.5

# Clean the build artifacts (preserves BMI cache)
xmake clean

# Delete the build directory (nukes BMI cache — use after code changes to avoid stale BMI issues)
rm -rf build

# Build the project(you can use -r for rebuild to reduce bugs)
xmake build

# Run all test cases(you can use -v for verbose)
xmake test

# After code changes: always rm -rf build before xmake build && xmake test
# Stale BMI cache can cause linker errors or constexpr mismatch diagnostics

# Run a .zero file (transpile + compile + execute)
xmake run zero run tests/test_helloworld.zero

# Other commands
xmake run zero dump <file>    # Dump token stream and AST

```

## Architecture

Layer responsibilities (dependency flows downward):
- `zero.common.*` — shared utilities: source spans, file/process helpers. No frontend/backend/driver imports
- `zero.frontend.lexer.*` — token types (`token`), lexer (`lexer`). No backend/driver imports
- `zero.frontend.parser.*` — AST nodes (`ast`), parser (`parser`), implementation partitions (`impl`/`decl`/`stmt`/`expr`). No backend/driver imports
- `zero.backend.*` — C++ code generation from AST. No file I/O, process execution, or CLI parsing
- `zero.driver.*` — orchestration: CLI entry (`zero.cpp`), command registry & help (`command`), command impls (`handler`), transpile pipeline (`pipeline`), C++ toolchain (`toolchain`).

### Key Design Decisions

- **Span lifecycle**: AST nodes store `Span` (byte offsets), not owned strings. Codegen extracts text via `text_at(source, span)`. The source string MUST outlive all AST references
- **Import pass-through**: `import` statements generate C++ module imports verbatim. The transpiler does not resolve or parse imported `.zero` files — this is a single-file model

## Coding Style

### Naming
- Functions: `snake_case`
- Types/Enums/Enum values: `PascalCase`
- Fields: `snake_case`
- Modules: `zero.<layer>.<name>`

For all C++ design conventions (initialization, type design, error handling, control flow, formatting, parameter passing), see [`cpp-design`](.claude/spec/cpp-design.md).

## Conventional Commits

Format: `<type>(<scope>, ...): <description>`
- Types: `feat`, `fix`, `refactor`, `chore`, `docs`, `ci`, `test`
- Scopes: match `src/` subdirectories or feature names
