# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

Carven is a programming language that transpiles `.cv` source files to standard C++ code. The transpiler itself is written in C++26 using modules (`.cppm` files) and builds with xmake.

Full language grammar and supported features are documented in [`docs/grammar.md`](docs/grammar.md).

## Build & Run

```shell
xmake f --toolchain=clang-cl # Windows
xmake f --toolchain=llvm # macOS

# Use after encountering BMI cache issues
rm -rf build

# Build the project
xmake build

# Run all test cases
xmake run carven-test

# Run a .cv file
xmake run carven run tests/helloworld.cv

# Dump tokens and AST
xmake run carven dump <file>

```

## Architecture

Layer responsibilities (dependency flows downward):
- `carven.common.*` — shared utilities: source spans, file/process helpers. No frontend/backend/driver imports
- `carven.frontend.token` — token type and formatters
- `carven.frontend.lexer` — lexer (tokenizer). No backend/driver imports
- `carven.frontend.ast` — AST node types and walker utilities
- `carven.frontend.parser` — parser, error types, arena allocators. No backend/driver imports
- `carven.backend.codegen` — C++ code generation from AST. No file I/O, process execution, or CLI parsing
- `carven.driver.command` — CLI commands and help text
- `carven.driver.pipeline` — transpile pipeline, flag parsing, Driver struct
- `carven.driver.handler` — command implementations (run, build, check)
- `carven.driver.dump` — token stream and AST dump
- `carven.driver.toolchain` — C++ compiler discovery and artifact path building

### Key Design Decisions

- **Span lifecycle**: AST nodes store `Span` (byte offsets), not owned strings. Codegen extracts text via `SourceFile::slice(span)`. The `SourceFile` MUST outlive all AST references
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
