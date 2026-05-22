# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

Carven is a programming language that transpiles `.cv` source files to standard C++ code. The transpiler itself is written in C++26 using modules (`.cppm` files) and builds with xmake.

Full language grammar and supported features are documented in [`docs/grammar.md`](docs/grammar.md).

## Build & Run

```shell
# Configure (Windows, first time or after clean)
xmake f --toolchain=clang-cl

# Configure (macOS) — adjust LLVM path to match installed version
xmake f --toolchain=llvm

# Clean all the build artifacts and toolchain configuration
# Nukes BMI cache (use after encouter an unexpected build error)
# Remeber to reset the toolchain configuration by using xmake f --toolchain
xmake clean --all

# Build the project(you can use -r for rebuild to reduce bugs)
xmake build

# Run all test cases(you can use -v for verbose)
xmake test

# Run a .cv file (transpile + compile + execute)
xmake run carven run tests/helloworld.cv

# Other commands
xmake run carven dump <file>    # Dump token stream and AST

```

## Architecture

Layer responsibilities (dependency flows downward):
- `carven.common.*` — shared utilities: source spans, file/process helpers. No frontend/backend/driver imports
- `carven.frontend.lexer.*` — token types (`token`), lexer (`lexer`). No backend/driver imports
- `carven.frontend.parser.*` — AST nodes (`ast`), parser (`parser`), implementation partitions (`impl`/`decl`/`stmt`/`expr`). No backend/driver imports
- `carven.backend.*` — C++ code generation from AST. No file I/O, process execution, or CLI parsing
- `carven.driver.*` — orchestration: CLI entry (`carven.cpp`), command registry & help (`command`), command impls (`handler`), transpile pipeline (`pipeline`), C++ toolchain (`toolchain`).

### Key Design Decisions

- **Span lifecycle**: AST nodes store `Span` (byte offsets), not owned strings. Codegen extracts text via `text_at(source, span)`. The source string MUST outlive all AST references
- **Import pass-through**: `import` statements generate C++ module imports verbatim. The transpiler does not resolve or parse imported `.cv` files — this is a single-file model

## Coding Style

### Naming
- Functions: `snake_case`
- Types/Enums/Enum values: `PascalCase`
- Fields: `snake_case`
- Modules: `carven.<layer>.<name>`

For all C++ design conventions (initialization, type design, error handling, control flow, formatting, parameter passing), see [`cpp-design`](.claude/specs/cpp-design-philosophy.md).

## Conventional Commits

Format: `<type>(<scope>, ...): <description>`
- Types: `feat`, `fix`, `refactor`, `chore`, `docs`, `ci`, `test`
- Scopes: match `src/` subdirectories or feature names
