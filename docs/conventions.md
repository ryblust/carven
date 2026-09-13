# C++ Conventions

These rules apply to project-authored C++ in `src/`, `tests/`, and `crafts/`.
Vendored source and fixtures that preserve an external interface follow their
owning format. Generated code follows the output baseline below; source-layout
and style rules do not apply to generated artifacts.

## C++ baseline

- Source in `src/` and `tests/internal/` may use C++26.
- Crafts, installed support source, generated code, and baseline consumer
  fixtures must compile with C++20. Implementations may select newer facilities
  when the consumer's standard and library support them, while preserving the
  Carven operation's semantics and functionality. Prefer library feature
  detection for library capabilities.
- The consumer project selects its C++ standard. Optional implementations do
  not raise the required baseline. Explicit newer-standard interop targets may
  exercise APIs from that standard.

## Source layout

- An owner directory contains files for one responsibility. A branch directory
  contains subdirectories. Split owners only into independently understandable
  responsibilities.
- Mixed file and directory layouts are permitted at the repository root, the
  `src/` compiler entry boundary, and where required by build metadata,
  test-group harnesses, or fixtures whose layout is under test.
- Place a contract and its implementation together. A contract may have
  multiple implementation slices within the same owner directory.

## Modules and visibility

- `src/carven.cppm` is the compiler's primary interface. Other compiler module
  units are partitions.
- Use `.cppm` for contract partitions and `.cpp` for implementation partitions.
  Implementation partition names end in `.impl`. File stems match the final
  module segment before `.impl`.
- Put imports in one block after the module declaration. Order partitions
  lexically and put `import std;` last. Import only dependencies used by the
  unit. An implementation partition imports its contract.
- Use a global module fragment only for macro-only test headers and required
  platform headers.
- Put translation-unit-private declarations in an anonymous namespace. Close
  anonymous namespaces with a namespace comment; named namespace closing
  comments are optional.
- In support headers, `detail` contains helpers used only by that header.
  Cross-header dependencies use named contracts in the owning namespace.
- Helpers used only by one class belong in its private scope where C++
  template rules permit it.
- Expose operations needed by production callers through the owning interface.
  Published state has one authoritative query surface. Update compiler-private
  interfaces together with their callers.

## Naming

| Kind | Form | Example |
| --- | --- | --- |
| Type, class, enum | `UpperCamelCase` | `SourceManager` |
| Function, member, local, parameter, file, directory | `lower_snake_case` | `source_id` |
| Enum case | `UpperCamelCase` | `TokenKind::NumberLiteral` |

- Preserve acronyms: `AST`, `IR`, `CV`, `ID`, and `UTF8`.
- ID type names end in `ID`. Use `id` when context identifies its role;
  otherwise qualify the role, as in `module_id`. ID collections use `_ids`.
- Use the same domain vocabulary in directories, module partitions, types,
  operations, and tests. Use `decl`, `expr`, and `stmt` for those concepts in
  directory names, module segments, and file stems.
- Name builders for their results and analyzers for their scope. Name
  implementation slices for their responsibility. Do not introduce plural long
  forms merely to distinguish an owner from its vocabulary.
- Avoid catch-all directory names such as `core`, `common`, `util`, and `misc`.
- Compiler module-name segments are non-keyword C++ identifiers. Avoid the bare
  identifier `module`; use a name that states its role.
- Protocol names and fixture spellings under test retain their required form.

## Declarations

- In compiler source, declare ordinary member functions in the class and define
  them out of class in `.cpp`, including constructors and single-line accessors.
  Private helper classes in `.cpp` follow the same rule in that file.
- Keep template and `constexpr`/`consteval` definitions visible where needed.
  Keep `= default` and `= delete` in the class declaration.
- Keep consecutive member function declarations together without blank lines.
  Separate access sections, documented groups, and data members as needed.
- Named functions use trailing return types where C++ permits. Constructors,
  destructors, and conversion functions are exceptions.
- Single-argument constructors other than copy and move constructors are
  `explicit`. Template type parameters use `typename`.
- Project-defined classes and structs are `final` unless a C++ protocol
  requires inheritance.
- Use `struct` for transparent aggregate records and `class` for types that
  own invariants or a lifecycle.
- Every noncapturing lambda in `src/` and `tests/internal/` is `static`.

## Values and initialization

- Use `const auto` for immutable value locals. Use `auto` for values that are
  modified, moved from, or mutably borrowed.
- `auto` may deduce a pointer; `auto*` is optional. Apply top-level constness to
  the pointer value independently of pointee constness. Preserve pointee
  constness through deduction; spell a pointer type explicitly when narrowing
  mutable access to the pointee.
- Use designated initializers in declaration order for non-empty project
  records, empty braces for fieldless values, and braces for container literals.
- Supply required construction facts explicitly. Default member initializers
  express option defaults or initial producer-private state, not missing
  required facts or unfinished analysis.
- When an integer literal's type is intentional, use a lowercase literal suffix
  such as `u`, `ll`, `ull`, or `uz` instead of constructing a fixed-width alias
  solely to type the literal.
- Compiler-owned size literals use `uz`. Consumer-facing source uses deduction
  or an explicitly typed `std::size_t` value.
- Use `static_cast` for explicit conversions it supports.

## Ownership and borrowing

- Variable-size owned storage uses standard value containers.
- Borrowed storage remains alive and valid throughout every use. State borrow
  lifetimes and invalidating operations at the owning interface. Do not return
  borrows into temporaries or local storage that dies on return.
- Treat moving or consuming an owner as ending its outstanding borrows.
- Construction-time readers return values when subsequent container growth
  could invalidate an exposed borrow.
- Pass trivial values by value. For borrowed input, use `std::string_view` for
  text and `std::span<const T>` for read-only ranges. Use references for required
  object borrows and pointers for nullable object borrows. Do not use output
  parameters.
- Use `const auto&` for read-only lvalue borrows.

## Failure handling

- Project implementation functions are `noexcept` unless a consumed C++
  protocol requires a conditional exception specification.
- Defaulted special members use the native exception specification unless an
  explicit protocol boundary requires `noexcept`; deleted members need none.
- Template requirements follow the operations needed by the consumer. A
  `noexcept` template may accept potentially throwing operations when escaping
  exceptions are defined to terminate.
- Compiler code does not use `throw`, `try`, or `catch`.
- Failed internal invariants terminate.
- Interop tests may enable C++ exceptions in isolated processes to verify
  exception boundaries.

## Control flow

- Return early for validation and failure propagation.
- Prefer prefix increment. Use postfix increment only when its previous value
  is required.
- Use `std::unreachable()` after an exhaustive enum switch only when every
  branch transfers control and falling through the switch is impossible.
- Call `std::get` only immediately after checking the active alternative;
  otherwise use `std::get_if`.
- Preserve evaluation order when simplifying expressions.

## Tests

Name C++ test cases `"Area: behavior"`. Use `REQUIRE` for premises and `CHECK`
for conclusions. Name table data before iterating it.

## Comments

Explain hidden constraints, invariants, or workarounds. Do not retain removed
code or add decorative separators.
