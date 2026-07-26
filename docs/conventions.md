# C++ Conventions

This document defines conventions for project-authored C++ under `src/`,
`tests/`, and `crafts/`. Vendored sources, generated artifacts, and fixtures
that preserve an external interface follow their owning format.

## C++ versions

- Source under `src/` and `tests/internal/` may use C++26.
- Crafts, installed support source, and consumer fixtures use only features
  admitted by the C++20 generated-source baseline.
- Do not add standard-version macros, compatibility branches, or parallel
  implementations for newer consumer modes.
- Every noncapturing lambda in `src/` and `tests/internal/` is `static`.
- Compiler-owned size literals use `uz`. Consumer-facing source uses deduction
  or an explicitly typed `std::size_t` value.

## Borrowing and storage

- References, pointers, spans, and string views are borrows. Their owner
  outlives every use.
- A required borrow is a reference. A nullable borrow is a raw pointer.
- Variable-size owned storage uses a standard value container.
- A function does not return a view into temporary or producer-private state.
- Do not add a compatibility adapter, alias, branch, or parallel path for a
  compiler-private interface.

Required construction facts are explicit at every construction site. A
required fact does not use a default value to mean unfinished analysis.
Producer-private mutable state may use defaults that express its initial
lifecycle state. Publication exposes one authoritative query surface.

## Errors and termination

- Project implementation functions are `noexcept` unless a consumed C++
  protocol requires a conditional exception specification.
- Compiler code does not use `throw`, `try`, or `catch`.
- Failed internal invariants terminate.
- Defaulted and deleted special members do not repeat meaningless `noexcept`.
- A consumer-facing template uses the operation's conditional `noexcept` or
  admits only nothrow types.
- An unconditional `noexcept` function does not call a potentially throwing
  operation on an admitted type.

## Files and modules

- `src/carven.cppm` is the primary interface. Other compiler module units are
  partitions.
- Use `.cppm` for contract partitions and `.cpp` for out-of-line implementation
  partitions. Implementation partition names end in `.impl`.
- Place a contract and implementation pair together and give both the final
  module-segment stem.
- A source-tree node names one ownership boundary. Split it only into durable,
  independently understandable responsibilities.
- An owner directory contains files and no subdirectories.
- A branch directory contains subdirectories and no files.
- Do not mix files and subdirectories at the same level.
- Exceptions are the repository root, the `src/` compiler entry boundary,
  documented build metadata, and fixtures whose layout is under test.
- One contract may have multiple owner-local implementation slices. A file
  split does not create another owner or facade without a distinct contract.
- Do not use catch-all directory names such as `core`, `common`, `util`, or
  `misc`.
- Every module-name segment is a non-keyword C++ identifier. Do not use the bare
  identifier `module`; state the role with `module_id`, `hir_module`,
  `module_record`, or another precise name.
- Put imports in one lexical block after the module declaration. Order
  partitions by name and put `import std;` last. Import only names used by the
  unit. An implementation partition imports its contract.
- Use a global module fragment only for macro-only test headers and required
  platform headers.
- Put translation-unit-private declarations in an anonymous namespace. Close
  anonymous namespaces with a namespace comment; a named namespace closing
  comment is optional.

## Names

| Kind | Form | Example |
| --- | --- | --- |
| Type, class, enum | `UpperCamelCase` | `SourceManager` |
| Function, member, local, parameter, file | `lower_snake_case` | `source_id` |
| Enum case | `UpperCamelCase` | `TokenKind::NumberLiteral` |

Acronyms remain intact: `AST`, `IR`, `CV`, `ID`, and `UTF8`. ID type names end
in `ID`; one ID variable ends in `_id`, and an ID collection ends in `_ids`.
Use the established abbreviations `decl`, `expr`, and `stmt`.

## Declarations and values

- Named functions use trailing return types where C++ permits. Constructors,
  destructors, and conversion functions are exceptions.
- One-argument constructors are `explicit`. Template parameter lists use
  `typename`.
- Project-defined classes and structs are `final` unless a C++ protocol
  requires inheritance.
- A `struct` is a transparent aggregate record. A `class` owns invariants or a
  lifecycle. Do not encode hidden semantic policy in a record's default member
  initializers; defaults are limited to genuine option defaults or initial
  producer-private state.
- Use `const auto` for immutable value locals.
- Use `auto` for values that are modified, moved from, or mutably borrowed.
- Use `const auto*` for read-only raw-pointer borrows and `auto*` for mutable
  raw-pointer borrows. Do not add top-level const to a local raw-pointer borrow.
- Use `const auto&` for read-only lvalue borrows.
- When an integer literal's type is intentional, use a lowercase literal suffix
  such as `u`, `ll`, `ull`, or `uz` instead of constructing a fixed-width alias
  solely to type the literal.
- Pass trivial values by value, strings as `std::string_view`, read-only ranges
  as `std::span<const T>`, required borrows by reference, and nullable
  non-owning borrows by pointer. Do not use output parameters.
- Use designated initializers in declaration order for non-empty project
  records. Use empty braces for fieldless values and braces for container
  literals.
- Required record fields do not use default member initializers to represent
  missing construction facts.
- Use `static_cast` for cross-type conversions.

## Control flow

- Return early for validation and failure propagation.
- Prefer prefix increment. Use postfix increment only when its previous value
  is required.
- End an exhaustive enum switch with `std::unreachable()`.
- Call `std::get` only immediately after checking the active alternative;
  otherwise use `std::get_if`.
- Do not change evaluation order to shorten an expression.

## Test source

Name cases `"Area: behavior"`. Use `REQUIRE` for premises and `CHECK` for
conclusions. Name table data before iterating it.

## Comments

Comments explain hidden constraints, invariants, or workarounds. Do not retain
removed code or add decorative separators.
