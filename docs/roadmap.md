# Roadmap

Implemented features are documented in [grammar.md](grammar.md).

## Proposals

| Feature | Status |
|---------|--------|
| Range-for loops `for i in 1..10` | [proposed](proposals/range-for.md) |
| Doc comments `///` / `//!` | [proposed](proposals/doc-comments.md) |
| Inline tests | [proposed](proposals/inline-test.md) |
| Inline C++ blocks | [proposed](proposals/inline-cpp.md) |
| Match `is` patterns | [proposed](proposals/match-is-patterns.md) |
| Carven runtime header | [proposed](proposals/runtime-header.md) |
| Lowering strategy | [proposed](proposals/lowering.md) |
| True `import std` | [proposed](proposals/true-import-std.md) |

## Planned

- Struct literals (`Point { x: 1, y: 2 }`)
- Generics / templates
- Traits / interfaces
- Lambdas
- Enum variants with payloads
- Module-level `const` and `static`
- `extern` function declarations
- Multi-file projects
- `export` for module authors
- `as` type casts
