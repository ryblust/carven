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
xmake run carven run <file>

# Dump tokens and AST
xmake run carven dump <file>

```

## Key Design Decisions

- **Span lifecycle**: AST nodes store `Span` (byte offsets), not owned strings. Codegen extracts text via `SourceFile::slice(span)`. The `SourceFile` MUST outlive all AST references.
- **Import pass-through**: `import` statements generate C++ module imports verbatim. The transpiler does not resolve or parse imported `.cv` files — this is a single-file model.
- **Arena allocator**: AST nodes allocated via bump-pointer `Arena`. Use `alloc<T>(args...)` — never `new` for AST nodes.
- **Context-free parser**: Parser distinguishes constructs by token type only, never by symbol table lookup. Semantic decisions deferred to codegen.
- **Monadic error handling**: `std::optional<T>` and `std::expected<T, E>`. No exceptions (`-fno-exceptions`). Every function `noexcept`.

## Coding Style

### Naming
- Functions: `snake_case`
- Types/Enums/Enum values: `PascalCase`
- Fields: `snake_case`
- Modules: `carven.<layer>.<name>`

C++ conventions are defined in two specs under `.claude/specs/`:

- [`cpp-design`](.claude/specs/cpp-design.md) — design philosophy and API patterns
- [`cpp-format`](.claude/specs/cpp-format.md) — formatting rules

All agents must apply both when writing, modifying, or reviewing C++ code.

Feature design proposals live in [`.claude/proposals/`](.claude/proposals/). When a proposal is fully implemented, delete it.

## Conventional Commits

Format: `<type>(<scope>, ...): <description>`
- Types: `feat`, `fix`, `refactor`, `chore`, `docs`, `ci`, `test`
- Scopes: match `src/` subdirectories or feature names
