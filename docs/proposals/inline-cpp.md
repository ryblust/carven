# Inline C++ Code

## Goal

Embed raw C++ code directly in Carven source files, serving as an "escape hatch" during transpilation—used when Carven syntax cannot express certain C++ features.

## Candidate Syntax

### Option A: C-style Preprocessor Directive

```carven
#cpp {
    // Raw C++ code, emitted verbatim to the generated C++ file
    auto lambda = [](int x) { return x * 2; };
}
```

### Option B: Rust-style Attribute

```carven
#[cpp] {
    auto lambda = [](int x) { return x * 2; };
}
```

### Option C: C++-style Attribute

```carven
[[cpp]] {
    auto lambda = [](int x) { return x * 2; };
}
```

## Comparison

| Dimension | A (`#cpp`) | B (`#[cpp]`) | C (`[[cpp]]`) |
|-----------|------------|--------------|---------------|
| Simplicity | High | Medium | Low |
| Consistent with C/C++ preprocessor style | Yes | No | No |
| Can embed expressions (non-block) | `#cpp(x + y)` | `#[cpp](x + y)` | Unnatural |
| Consistency with inline test syntax | High (if `#test` is chosen) | High (if `#[test]` is chosen) | High (if `[[test]]` is chosen) |

## Use Cases

- Calling C++ templates, concepts, requires clauses, etc., that Carven does not directly support
- Embedding lambda expressions (Carven does not yet support lambdas)
- Using C++-specific library APIs
- Hand-written C++ optimizations on performance-critical paths

## Notes

- Inline C++ code is **not** processed by Carven's lexer/parser—it is embedded directly in the output
- Braces `{}` inside inline C++ must be properly matched (otherwise they break the transpiled output structure)
- Overusing inline C++ undermines Carven's value—it should be a last resort

## Open Questions

- [ ] Syntax choice—should be consistent with inline tests
- [ ] Should expression form (`#cpp(x + y)`) be supported in addition to block form?
- [ ] Can Carven variables be used inside inline C++? (requires name mapping)
- [ ] Should an `unsafe` keyword wrap this? (if safety checks are added in the future)
