# zero

## Overview

A programming language that transpiles `.zero` source files to standard C++ code

## Architecture

Layer responsibilities:
- `zero.common.*`: shared low-level utilities, source spans, C++ standard helpers, file/process helpers. Must not import frontend/backend/driver modules
- `zero.frontend.*`: tokens, lexer, parser, and AST-like source structures. Must not import backend/driver modules
- `zero.backend.*`: C++ code generation from frontend structures. Must not perform file I/O, process execution, or CLI parsing
- `zero.driver.*`: orchestration for transpile/build/run flows: source loading coordination, artifact paths, codegen calls, compiler invocation, executable execution
- `zero.driver.cli`: CLI parsing, help text, and command dispatch only. Keep business logic out of CLI handlers

## Current Compiler Strategy

- Top-level declarations are parsed into AST-like items
- Function bodies are currently source-slice passthrough; preserve the user's original body text during codegen
- Do not introduce statement/expression parsing unless the task explicitly asks for language semantics
- Keep `zero run` working as the fastest feedback loop for implementation work

## C++ Coding Style

### Naming
- Functions: `snake_case`
- Types: `PascalCase`
- Fields: `snake_case`
- Enums: `PascalCase`
- Enum Value: `PascalCase`
- Modules: `zero.<layer>.<name>`

### Basic

- Use `const` or `constexpr` for every variable unless it is mutated
- Prefer `constexpr` for pure, no side-effect and no I/O functions.
- Use `noexcept` for project functions;
- Do not use `try catch` and `throw`
- Use `std::print` and `std::println` for output instead of `std::cout` and `printf`
- Use `std::optional` and `std::expected` for error handling
- Use pre-increment `++i` in for-loop expressions, not `i++`
- Prefer `std::ranges` and `std::views` APIs over raw loops and traditional <algorithm>; explicit loops are fine for lexing/parsing state machines and low-level system code
- Use braces `{}` when `if`/`else`/`for`/`while` body is on its own line; omit braces for single-line e.g. `if (x) return true;`
- Prefer non-owning view types (std::string_view, std::span) for read-only or borrowed function parameters
- Use `std::string_view` instead of `const std::string&`
- Use `std::span` instead of `const std::vector&`, `std::array` and C-style array
- Use pass-by-value for trivial types instead of pass-by-const-reference e.g. `auto foo(Token)` instead of `auto foo(const Token&)`
- Only comment non-obvious logic, intentional design decisions, and hidden constraints
- A magic number or a workaround for a specific compiler bug deserves a brief comment

## Building

The `zero` transpiler itself targets C++26 and builds via xmake.
```shell
xmake build              # Build the zero transpiler
xmake run zero [args...] # Run the executable and pass the args
```

## Avoid

- Do not add broad abstractions before a real second use case.
- Do not use shell-string command execution for compiler/process invocation; use argv-based process helpers.
- Do not put transpilation, artifact, compile, or run logic in CLI handlers.

## Commit Format

**Conventional commits**: `<type>(<scope>): <description>`
- **Types**: `feat`, `fix`, `refactor`, `chore`, `docs`, `ci`, `test`
- **Scopes**: match `src/` subdirectories or feature names
