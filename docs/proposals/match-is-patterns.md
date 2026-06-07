# Match `is` Patterns

## Goal

Make `match` patterns explicit enough to support both type tests and value tests
without relying on symbol-table lookup in the parser.

Current Carven treats identifier patterns as type patterns:

```carven
match x {
    i32 => { }    // type match
    zero => { }   // also a type match, not a variable value match
    _ => { }
}
```

This preserves the context-free parser, but it means `match` cannot currently
compare against a variable value.

## Proposed Direction

Introduce an `is` keyword for explicit runtime-backed pattern checks.

```carven
match x {
    is i32 => { }       // type test
    is zero => { }      // value test against variable `zero`
    0 => { }            // literal value test
    _ => { }
}
```

The parser can keep treating the syntax context-free: `is` marks a pattern that
requires semantic analysis to decide whether the operand names a type, a value,
or another supported pattern form.

## C++ Lowering Direction

This depends on a Carven runtime header. The runtime can provide helpers for
type and value classification, implemented with standard C++ facilities such as
concepts, `type_traits`, `std::same_as`, and overload sets.

Possible generated shape:

```cpp
if constexpr (carven::is_type<std::remove_cvref_t<decltype(value)>, T>) {
    // type arm
} else if (carven::is_value(value, expected)) {
    // value arm
}
```

Exact helper names and overload rules should be decided with the runtime header
design.

## Open Questions

- Should `is i32` and bare `i32` both remain valid type patterns, or should `is`
  become the preferred explicit form?
- How should sema resolve ambiguous names when both a type and value exist?
- Should `is` support future destructuring, enum payloads, or concepts?
- Should bare identifier value patterns ever be allowed, or should value matches
  always require `is` to preserve readability?
