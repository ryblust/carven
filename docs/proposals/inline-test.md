# Inline Tests

## Goal

Write unit tests directly in Carven source files, similar to Rust's `#[test]` or Zig's `test` blocks.

## Candidate Syntax

### Option A: C-style Preprocessor Directive

```carven
#test "test name" {
    let x = add(1, 2);
    assert(x == 3);
}
```

### Option B: Rust-style Attribute

```carven
#[test]
fn test_addition() {
    let x = add(1, 2);
    assert(x == 3);
}
```

### Option C: Keyword

```carven
test "test addition" {
    let x = add(1, 2);
    assert(x == 3);
}
```

### Option D: C++-style Attribute

```carven
[[test]]
fn test_addition() {
    let x = add(1, 2);
    assert(x == 3);
}
```

## Comparison

| Dimension | A (`#test`) | B (`#[test]`) | C (`test` keyword) | D (`[[test]]`) |
|-----------|-------------|---------------|---------------------|----------------|
| Simplicity | High | Medium | High | Low |
| Consistent with C/C++ preprocessor style | Yes | No | No | No |
| Consistent with Rust attribute style | No | Yes | No | Partial |
| Consistent with Zig test style | No | No | Yes | No |
| Conforms to C++ attribute syntax | No | No | No | Yes |
| Requires new token | `#test` | `#[test]` | `test` (keyword) | `[[test]]` (complex) |

## Open Questions

- [ ] Syntax choice
- [ ] Should `assert` be built-in, or rely on C++'s `assert` macro?
- [ ] Should test function parameters be supported (e.g., fixtures)?
- [ ] Should special markers like `should_panic` / `#[should_fail]` be supported?
