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

# Build the project
xmake build

# Rebuild the project
xmake build -r

# Run all test cases(-v for verbose)
xmake test -v

# Run a .zero file (transpile + compile + execute)
xmake run zero run tests/helloworld.zero

# Other commands
xmake run zero tokens <file>   # Lex and dump token stream
xmake run zero ast <file>      # Parse and dump AST
xmake run zero check <file>    # Parse without codegen

# Clean the build artifacts
xmake clean
```

Run flags: `-std=c++<value>` (14/17/20/23/26), `--output-dir=<path>`, `--no-default-include-std`

Build config in `xmake.lua`: RTTI disabled (`-fno-rtti` / `/GR-`), exceptions disabled (`no-cxx`), all-extra warnings, C++latest, default mode `debug`.

## Architecture

Layer responsibilities (dependency flows downward):
- `zero.common.*` — shared utilities: source spans, file/process helpers. No frontend/backend/driver imports
- `zero.frontend.*` — lexer (`lexer.cppm`), parser (`parser.cppm`), token types (`token.cppm`). No backend/driver imports
- `zero.backend.*` — C++ code generation from AST. No file I/O, process execution, or CLI parsing
- `zero.driver.*` — orchestration: CLI entry (`cli`), command registry & help (`command`), command impls (`handler`), transpile pipeline (`pipeline`), C++ toolchain (`toolchain`).

### Key Design Decisions

- **Source-slice passthrough**: function bodies are not parsed into statements/expressions. The parser records a `body_span` and codegen copies the original source text verbatim
- **AST structure**: `TopLevelItem = variant<ImportItem, EnumItem, StructItem, FunctionItem>`
- **Type mapping**: zero types (i32, f64, bool, etc.) map to C++ equivalents in codegen
- **Build artifacts**: output goes to `.zero/build/` with filename hashing for cache invalidation
- **Compiler selection**: uses `CXX` env var, falls back to `clang-cl` (Windows) or `c++` (other)
- **Span lifecycle**: AST nodes store `Span` (byte offsets into source), not owned strings. Codegen extracts text via `text_at(source, span)`. The source string MUST outlive all AST/symbol references
- **Import pass-through**: `import` statements generate C++ module imports verbatim. The transpiler does not resolve or parse imported `.zero` files — this is a single-file model

## Coding Style

### Naming
- Functions: `snake_case`
- Types/Enums/Enum values: `PascalCase`
- Fields: `snake_case`
- Modules: `zero.<layer>.<name>`

### C++ Conventions
- `const`/`constexpr` on all non-mutated variables; prefer `constexpr` for pure functions
- `noexcept` on project functions; no `try`/`catch`/`throw`
- `std::print`/`std::println` instead of `std::cout`/`printf`
- `std::optional` for optional values; custom result types with boolean/check methods for error handling (e.g., `TranspileResult::has_errors()`, `ProcessResult.started`)
- `std::string_view` over `const std::string&`; `std::span` over `const std::vector&`
- Prefer non-owning view types (std::string_view, std::span) for read-only or borrowed function parameters
- Pass-by-value for trivial types (e.g., `auto foo(Token)` not `auto foo(const Token&)`)
- Pre-increment `++i` in loops
- Prefer `std::ranges`/`std::views` over `<algorithm>`; explicit loops OK for lexing/parsing state machines or low-level
- Braces `{}` when body is on its own line; omit for single-line (e.g., `if (x) return true;`)
- `Type()` for default init, `Type{ .field = val }` for aggregate init with designated initializers
- Single-line initializer lists: no space between `>` and `{` — `std::vector<int>{ 1, 2, 3 }`
- Multi-line initializer lists: space between `>` and `{` — `std::vector<int> {` on the opening line
- Use `if`-with-initializer to limit variable scope and unwrap optionals:
  `if (const auto content = read_file(path); content) { ... } else { ... }`
- Don't bind intermediate variables for single-use values; pass them directly
- Range-for directly on function return temporaries: `for (const auto token : tokenize(source))`
- Don't wrap `const char*`/`string_view` in `std::filesystem::path` for function params; the implicit conversion suffices
- Omit type name in aggregate init when context provides it: `run_single_file({ .filename = ..., .content = ... })`
- Separate logical blocks within a function with a blank line

## Conventional Commits

Format: `<type>(<scope>, ...): <description>`
- Types: `feat`, `fix`, `refactor`, `chore`, `docs`, `ci`, `test`
- Scopes: match `src/` subdirectories or feature names
