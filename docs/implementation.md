# Implementation Model

This document defines stable engineering rules for Carven's implementation. The
core transpiler should stay pure, deterministic, and constexpr-friendly; runtime
integration should stay at the edges.

## Core Pipeline

The core transpiler path includes:

- tokenization
- parsing
- semantic checks
- lowering decisions that are pure source-to-model transformations
- C++ text generation
- `transpile(source, options)`

These parts should remain constexpr-friendly where practical. A target shape is:

```cpp
static_assert([] {
    const auto result = transpile("fn main() { return 0; }", {});
    return result.errors.empty();
}());
```

Runtime edges are outside the constexpr core:

- filesystem access
- process execution
- xmake invocation
- terminal output
- installation
- platform cache management

These edges should be thin wrappers over the pure core.

## Core Rules

- Prefer explicit inputs and return values over hidden global state.
- Keep source text owned outside AST nodes. AST nodes store spans.
- Keep diagnostics as values, not side effects.
- Keep parser decisions context-free. Name lookup and type checks belong after
  parsing.
- Keep codegen deterministic for the same AST, source text, and options.
- Use `constexpr` as a design promise for pure core logic, not as decoration for
  arbitrary runtime code.

## Modules

Carven is module-first. The project should not mechanically split every module
into `.cppm` interface files and `.cpp` implementation files.

For constexpr core code:

- keep exported APIs small
- keep required definitions reachable from module interfaces
- use non-exported declarations inside module interfaces when useful
- prefer module interface partitions over hidden implementation units when
  constant evaluation needs the definitions

For runtime-only code, `.cpp` files, private module fragments, or internal
module partitions can be used when they make the implementation clearer.

## Allocation

Transient allocation is acceptable inside constant evaluation as long as it does
not escape the immediate constexpr computation. `std::vector`, `std::string`,
and arena-backed AST construction can be useful inside `static_assert` tests
when the resulting owned objects do not need static storage.

The current arena design treats ASTs as short-lived compiler data. If Carven
later adds long-lived services such as an LSP or daemon mode, arena destruction
and non-trivial AST member cleanup must be revisited.

## Tests

Constexpr tests should complement runtime tests:

- constexpr unit tests cover small tokenization, parsing, sema, and codegen
  examples
- golden tests cover generated C++ text at runtime
- e2e tests compile and run generated programs

Inline language tests can follow the same principle later: pure compile-time
examples first, runtime integration second.
