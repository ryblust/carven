# Carven Lowering Strategy

## Goal

Define how Carven preserves high-level language semantics while generating C++
for different target standard levels.

Carven should let users write the same source regardless of whether the current
xmake project targets C++26, C++23, C++20, or a lower supported backend. The
lowering layer is where Carven decides which C++ shape can implement that
semantic contract for the selected standard.

## Starting Point

The first version can stay simple: codegen checks the requested C++ standard and
emits different C++ text with straightforward conditionals.

For example, `fn main(args)` can initially target a non-owning range view:

```cpp
const auto args =
    std::views::iota(1, argc)
    | std::views::transform([argv](int index) noexcept {
          return std::pair<std::size_t, std::string_view> {
              static_cast<std::size_t>(index),
              std::string_view(argv[index]),
          };
      });
```

Older target standards can later lower the same Carven `args` semantics to a
different implementation shape, such as a small runtime helper or an explicit
loop.

## Direction

As features grow, lowering should become a distinct conceptual layer between
parsed/sema-checked Carven syntax and final C++ text generation.

This does not need to copy a general-purpose compiler IR. Carven is a
transpiler, so the useful middle layer may be a smaller intermediate semantic
model: enough to represent Carven constructs such as `main(args)`, future range
loops, match helpers, runtime assertions, and standard-library fallbacks without
forcing every decision directly into string-oriented codegen.

## Runtime Relationship

The runtime header is a toolbox for lowering, not the lowering layer itself.
Lowering decides which semantic implementation is needed for a target C++
standard; the runtime header may provide small helper types or functions used by
that implementation.

## Open Questions

- Which C++ standard levels should Carven officially support as backend targets?
- When should simple codegen conditionals be replaced by an intermediate
  semantic model?
- Should lowering decisions live in codegen at first, or in a separate module
  such as `carven.backend.lowering`?
- How should golden tests represent multiple backend standards for the same
  Carven source?
