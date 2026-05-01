# zero

## Overview

A programming language that transpiles `.zero` source files to standard C++ code

## C++ Coding Style

### Naming Convention
- Functions: `snake_case`
- Types: `PascalCase`
- Enums: `PascalCase`
- Modules: `zero.<layer>.<name>`

### Basic
- Use `const` or `constexpr` for every variable unless it is mutated
- Use `constexpr` for pure, no side-effect and no I/O functions
- Use `noexcept` for every function, no `try catch` and `throw`
- Use `std::print` and `std::println` for output instead of `std::cout` and `printf`
- Use `std::optional` and `std::expected` for error handling
- Use pre-increment `++i` in for-loop expressions, not `i++`
- Use `std::ranges` and `std::views` APIs over raw loops and traditional <algorithm>
- Use braces `{}` when `if`/`else`/`for`/`while` body is on its own line; omit braces for single-line e.g. `if (x) return true;`
- Prefer non-owning view types (std::string_view, std::span) for read-only or borrowed function parameters
- Use `std::string_view` instead of `const std::string&`
- Use `std::span` instead of `const std::vector&`, `std::array` and C-Style array
- Use pass-by-value for trivial types instead of pass-by-const-reference e.g. `auto foo(Token)` instead of `auto foo(const Token&)`
- Only comment non-obvious logic, intentional design decisions, and hidden constraints
- A magic number or a workaround for a specific compiler bug deserves a brief comment

## Build System
```shell
xmake build              # Build the zero transpiler
xmake run zero [args...] # Run the executable and pass the args
```

## Commit Format
**Conventional commits**: `<type>(<scope>): <description>`
- **Types**: `feat`, `fix`, `refactor`, `chore`, `docs`, `ci`, `test`
- **Scopes**: match `src/` subdirectories or feature names