# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

Zero is a programming language that transpiles `.zero` source files to standard C++ code. The transpiler itself is written in C++26 using modules (`.cppm` files) and builds with xmake.

## Build & Run

```shell
# Configure (Windows, first time or after clean)
xmake f --toolchain=clang-cl -m debug

# Configure (macOS)
xmake f --toolchain=llvm --sdk=/opt/homebrew/Cellar/llvm/22.1.4 -m debug

# Build the transpiler
xmake build

# Run a .zero file (transpile + compile + execute)
xmake run zero run tests/helloworld.zero

# Other commands
xmake run zero tokens <file>   # Lex and dump token stream
xmake run zero ast <file>      # Parse and dump AST
xmake run zero check <file>    # Parse without codegen
```

Run flags: `-std=c++XX` (14/17/20/23/26), `--output-dir <path>`, `--no-default-include-std`

## Architecture

Layer responsibilities (dependency flows downward):
- `zero.common.*` — shared utilities: source spans, file/process helpers. No frontend/backend/driver imports
- `zero.frontend.*` — lexer (`token.cppm`), parser (`parser.cppm`), token types. No backend/driver imports
- `zero.backend.*` — C++ code generation from AST. No file I/O, process execution, or CLI parsing
- `zero.driver.*` — orchestration: transpile/build/run pipeline, CLI dispatch

### Key Design Decisions

- **Source-slice passthrough**: function bodies are not parsed into statements/expressions. The parser records a `body_span` and codegen copies the original source text verbatim
- **AST structure**: `TopLevelItem = variant<ImportItem, EnumItem, StructItem, FunctionItem>`
- **Type mapping**: zero types (i32, f64, bool, etc.) map to C++ equivalents in codegen
- **Build artifacts**: output goes to `.zero/build/` with filename hashing for cache invalidation
- **Compiler selection**: uses `CXX` env var, falls back to `clang-cl` (Windows) or `c++` (other)

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
- `std::optional`/`std::expected` for error handling
- `std::string_view` over `const std::string&`; `std::span` over `const std::vector&`
- Prefer non-owning view types (std::string_view, std::span) for read-only or borrowed function parameters
- Pass-by-value for trivial types (e.g., `auto foo(Token)` not `auto foo(const Token&)`)
- Pre-increment `++i` in loops
- Prefer `std::ranges`/`std::views` over `<algorithm>`; explicit loops OK for lexing/parsing state machines or low-level
- Braces `{}` when body is on its own line; omit for single-line (e.g., `if (x) return true;`)
- `Type()` for default init, `Type{ .field = val }` for aggregate init with designated initializers

## Conventional Commits

Format: `<type>(<scope>): <description>`
- Types: `feat`, `fix`, `refactor`, `chore`, `docs`, `ci`, `test`
- Scopes: match `src/` subdirectories or feature names

## Avoid

- Don't add broad abstractions before a second use case
- Don't use shell-string command execution; use argv-based process helpers
- Don't put transpilation/compile/run logic in CLI handlers
- Don't introduce statement/expression parsing unless explicitly asked
