# zero

A programming language that transpiles `.zero` source files to C++20 Modules code. Written in C++26, built with xmake.

## Build

```bash
xmake build                       # Build
xmake run zero run <file.zero>    # Build and run a .zero file
xmake run zero                    # Run the toolchain binary
xmake f && xmake build            # Reconfigure + rebuild
```

## Architecture

```
src/common/          Shared utilities
src/driver/          CLI and pipeline orchestration
src/frontend/        Lexing, parsing
src/backend/         Code generation
src/zero.cpp         Entry point
```

### Pipeline

```
source string → tokenize → parse → generate → C++ output
```

Entry point: `transpile()` in `zero.driver.pipeline` — pure `constexpr` function: `std::string_view → std::string`.

### Source model

AST types store `Span` (not `std::string_view`) to avoid lifetime coupling. A `Span` is just two offsets — it doesn't know about the text. Callers extract text via `std::string_view::substr(span.start, span.end - span.start)`. `SourceFile` is a CLI-layer convenience for bundling `filename + content`; the compiler core (`parse`, `generate`) operates on raw `std::string_view`.

### Interface design

Each compiler stage depends only on the data it consumes, not on convenience wrappers. This keeps interfaces minimal and testing trivial:

```
tokenize(std::string_view)             → std::vector<Token>
parse(std::span<Token>, string_view)   → std::vector<TopLevelItem>
generate(std::span<TopLevelItem>, string_view) → std::string
```

- `parse` and `generate` take `std::string_view` — they don't know `SourceFile` exists
- No stage couples to the layer above it: lexer doesn't know about files, parser doesn't know about CLI
- Testing a stage requires only a string literal — no file, no SourceFile, no setup

### Module naming

`zero.<layer>.<name>` — e.g. `zero.frontend.lexer`, `zero.backend.codegen`.

## Zero language syntax

- `import <module> using { name1, name2 }` — imports
- `fn name(param: Type) -> Ret { ... }` — functions
- `let` (immutable), `var` (mutable), `const` (compile-time) — bindings
- `if`/`else`, `while`, `for ... in`, `return` — control flow
- `value as Type` — type cast
- `//` line comments only
- 18 keywords: `import`, `export`, `using`, `enum`, `struct`, `fn`, `var`, `let`, `const`, `as`, `if`, `else`, `while`, `for`, `return`, `true`, `false`

## Conventions

### Naming

- Functions: `snake_case`, verb-first (`tokenize`, `transpile`, `generate`)
- Types: `PascalCase` (`Token`, `SourceFile`, `DiagnosticEngine`)
- Enums: `PascalCase` for both type and values
- Modules: `zero.<layer>.<name>`

### Const first

Every local variable is `const` unless it must be mutated. If you can write `const auto`, do it. Only plain `auto` when the variable is reassigned, incremented, or moved-from (e.g. string builders, loop counters, `std::move` destinations).

### Functions

- Pure transformations are `constexpr noexcept` with `auto` return type
- Pass `std::string_view` and `std::span`; return by value
- Pass-by-value for trivial types; `const&` otherwise
- Unused parameters: `[[maybe_unused]]` attribute, not `/*name*/` comments

### Formatting

- `Type {` with space before `{` on aggregates spanning multiple lines; `Type{` fine for single-line (`Span{}`)
- Designated initializers: one field per line; space-separated unless clean column alignment is possible
- Anonymous `namespace { }` for internals; only the public entry point is exported
- Use braces `{}` when `if`/`else`/`for`/`while` body is on its own line; omit braces for single-line (`if (x) return true;`)
- Init-statement in `if` when a variable is only used in that branch: `if (const auto& x = expr; condition)`
- Prefer chaining on `std::string` (e.g. `return result.append("\n");`) over separate push_back + return

### Includes and I/O

- No `<iostream>` (`std::cout`, `std::cin`, `std::cerr`) or C-style I/O (`printf`)
- Use `std::println`/`std::print`; error failures signaled by return code, not `stderr`
- `std::optional` for fallible operations that return a value
- Prefer `std::ranges` algorithms over `<algorithm>`

### Comments

Only comment non-obvious logic, intentional design decisions, and hidden constraints. A magic-number offset or a workaround for a specific compiler bug deserves a brief comment.

Do NOT: comment what code does (names should carry that), write docstrings (`///`) or multi-line blocks, leave commented-out code or `TODO` blocks in active code.

## Commit format

**Conventional commits**: `<type>(<scope>): <description>`

- **Types**: `feat`, `fix`, `refactor`, `chore`, `docs`, `ci`, `test`
- **Scopes**: match `src/` subdirectories or feature names
