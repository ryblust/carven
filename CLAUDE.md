# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

Zero is a programming language that transpiles `.zero` source files to standard C++ code. The transpiler itself is written in C++26 using modules (`.cppm` files) and builds with xmake.

## Build & Run

```shell
# Configure (Windows, first time or after clean)
xmake f --toolchain=clang-cl

# Configure (macOS) — adjust LLVM path to match installed version
xmake f --toolchain=llvm --sdk=/opt/homebrew/Cellar/llvm/22.1.4

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

Run flags: `-std=c++<value>` (14/17/20/23/26), `--output-dir=<path>`, `--no-default-include-std`

Build config in `xmake.lua`: RTTI disabled (`-fno-rtti` / `/GR-`), exceptions disabled (`no-cxx`), all-extra warnings, C++latest, default mode `debug`.

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

### C++ Conventions

**Types & qualifiers**
- `const`/`constexpr` on all non-mutated variables; prefer `constexpr` for pure functions
- `noexcept` on project functions; no `try`/`catch`/`throw`
- `std::optional` for optional values; custom result types with boolean/check methods for error handling (e.g., `TranspileResult::has_errors()`, `ProcessResult.started`)
- Pass-by-value for trivial types (e.g., `auto foo(Token)` not `auto foo(const Token&)`)

**Parameters & views**
- `std::string_view` over `const std::string&`; `std::span` over `const std::vector&`
- Don't wrap `const char*`/`string_view` in `std::filesystem::path` for function params

**Syntax & formatting**
- `Type()` for default init, `Type{ .field = val }` for aggregate init with designated initializers
- Single-line initializer lists: no space between `>` and `{` — `std::vector<int>{ 1, 2, 3 }`
- Multi-line initializer lists: space between `>` and `{` — `std::vector<int> {` on the opening line
- Braces `{}` when body is on its own line; omit for single-line (e.g., `if (x) return true;`)
- Pre-increment `++i` in loops
- Omit type name in aggregate init when context provides it: `run_single_file({ .filename = ..., .content = ... })`

**Control flow & scope**
- Use `if`-with-initializer to limit variable scope and unwrap optionals:
  `if (const auto content = read_file(path); content) { ... } else { ... }`
- Don't bind intermediate variables for single-use values; pass them directly
- Range-for directly on function return temporaries: `for (const auto token : tokenize(source))`

**API surface**
- `std::print`/`std::println` instead of `std::cout`/`printf`
- Prefer `std::ranges`/`std::views` over `<algorithm>`; explicit loops OK for lexing/parsing state machines or low-level
- Separate logical blocks within a function with a blank line

## Conventional Commits

Format: `<type>(<scope>, ...): <description>`
- Types: `feat`, `fix`, `refactor`, `chore`, `docs`, `ci`, `test`
- Scopes: match `src/` subdirectories or feature names
