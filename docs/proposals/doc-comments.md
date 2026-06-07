# Doc Comments

## Goal

Add doc comment syntax `//!` and `///` to Carven for generating documentation from source code.

## Proposed Syntax

```carven
//! Module-level doc comment (describes the current module's functionality and purpose)

/// Function-level doc comment (describes the function declared below)
fn add(a: i32, b: i32) -> i32 {
    return a + b;
}

/// Struct doc comment
struct Point {
    /// Field doc comment
    x: f64,
    y: f64,
}
```

## Rules

- `//!` — Module/file-level documentation, describes the current module
- `///` — Item-level documentation, describes the immediately following declaration (function, struct, enum, etc.)
- Markdown formatting is supported inside doc comments

## Transpilation Strategy

- Embedded as special comments in the generated C++ code
- Can be used in the future for auto-generating documentation (similar to `rustdoc`)

## Open Questions

- [ ] Should we adopt Rust's `//!` / `///` convention?
- [ ] Do we need `/** */` block doc comments?
- [ ] Should doc comments be included in the generated C++?
- [ ] Do we need tags like `@param` / `@return`, or plain Markdown only?
- [ ] Should the lexer distinguish doc comments from regular comments? Or should the parser handle this?

## References

- Rust: `//!` (inner doc) / `///` (outer doc)
- Doxygen/Javadoc: `/** */` with `@` tags
