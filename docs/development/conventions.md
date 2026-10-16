# Source conventions

This document defines project source layout, Carven source style, and handwritten
C++ conventions.

## Scope and formatting

| Source | Applicable rules |
| --- | --- |
| Project source in `src/`, `tests/`, `crafts/`, `benchmarks/`, and `tools/` | Apply these source conventions |
| Vendored source and external-interface fixtures | Follow their owning format |
| Generated artifacts | Apply the C++ baseline and header-order rules |

Use `.clang-format` for C++ formatting and neighboring code for local idioms.

## C++ baseline

| Source | Requirement |
| --- | --- |
| `src/`, `tests/internal/`, and `tools/` | May use C++26 |
| Crafts, installed support source, generated code, baseline consumer fixtures, and native benchmarks | Must compile with C++20 |

Select newer facilities only when the consumer's standard and library support them,
preserving the operation's semantics and functionality. Prefer library feature
detection. Let consumers select their C++ standard; optional implementations must
not raise the baseline. Explicit newer-standard interop targets may exercise that
standard's APIs.

## Source layout

- Owner directories contain files for one responsibility; branch directories
  contain subdirectories. Split owners only at independently understandable
  responsibility and dependency boundaries. Avoid catch-all directory names.
- Keep an owner's entry, state, contracts, and implementation slices together.
  Split files by coherent operations, algorithms, or contracts, defining state
  ownership and dependency direction across slices.
- Mixed file and directory layouts are permitted at the repository root, the
  `src/` compiler entry boundary, and where required by build metadata,
  test-group harnesses, or fixtures whose layout is under test.

### Native interoperation

- Put native type and function declarations in `.hpp` files and ordinary
  implementations in consumer-selected `.cpp` files. Keep templates visible in headers.
- Import native headers from `.cv` files. Declare `import(cpp)` boundaries when
  source access, failure, or async contracts are needed.
- Standard Craft bridges use `carven::standard::<domain>`; shared builtin support
  uses `carven::runtime::<domain>`. Bind `import(cpp)` declarations to qualified
  native function names, independently of language module paths. Use `std::` for
  C++ standard-library references; avoid native namespaces that shadow it.
- Use source fragments for small module-local adapters and implementation-only
  includes, including headers that depend on generated Carven signature types.
  Publish native declarations through headers; fragments remain implementation-only.

### Runtime domains

Keep only domain directories under `crafts/carven/runtime/`. Group related
contracts and implementation slices by domain:

| Domain | Responsibility |
| --- | --- |
| `call/` | Callable views and native parameter passing |
| `collection/` | Arrays, slices, vectors, ranges, and checked indexing |
| `diagnostics/` | Runtime checks, assertion observations, and failure reports |
| `display/` | Bounded structural value display |
| `execution/` | Program entry, control support, asynchronous operations, and timers |
| `format/` | General formatting and prepared fragment writing |
| `io/` | Host input cursor and scheduling, native read operations, and printing |
| `numeric/` | Scalar numeric operations and arithmetic checks |
| `simd/` | Vector and mask operations with native and portable backends |
| `testing/` | Inline-test execution and reporting contexts |
| `text/` | String storage, borrowed text views, and UTF-8 operations |
| `value/` | Value representations, result storage, and stateless values |

Expose self-contained contract headers such as `collection/array.hpp` and
`format/writer.hpp`. Consumers include the contracts they use. Keep implementation
headers internal to the domain, using an entry such as `text/text.hpp` when they
share one contract. Compiler source in `src/` does not include Crafts headers;
generated programs and source Crafts consume them through native interoperation.
Library-specific host bridges and platform dependencies stay with their Craft.

Use angle brackets and complete `carven/` paths, including within a domain. Native
consumers add the Crafts root to their include paths; Carven source uses native imports:

```cpp
#include <carven/runtime/collection/array.hpp>
```

```carven
import <carven/std/net/native.hpp>;
```

### Header order

Order independent includes by category, then lexically by path:

1. The implementation's associated interface header, when one exists.
2. Carven runtime headers.
3. Other generated Carven API and runner headers.
4. Other project and craft headers, then third-party headers.
5. C++ standard-library headers, then platform headers.

Public headers supply their own dependencies. Keep independent Carven-owned headers
in one block by default; separate external library headers and any needed authored
groups with one blank line. Preserve ordering required by macros, conditional
compilation, and implementation-header dependencies. Generated artifacts place
explicit native import environments, in source order, after the associated header
and before synthesized dependency groups. Formatters preserve include order.

### Carven source style

Write combined function modifiers as `const async fn`, with static capability
before execution kind.

Group imports in official source in this order:

1. Modules in the current craft, using unprefixed or leading-`.` references.
2. Modules in other crafts, using craft-qualified references.
3. Standard-library modules, using `std::`.
4. Native C++ headers.

Sort module references lexically within each group. Use one blank line to
separate authored groups when needed. Keep each selection list with its module
import. Preserve the required order of native headers; order independent headers
by the header categories above.
Keep `std::` imports in the standard-library group within the official craft
as well.
The formatter preserves the written order.

## Modules and visibility

- `src/carven.cppm` is the compiler's primary interface. Other compiler module
  units are partitions.
- Use `.cppm` for contract partitions and `.cpp` for implementation partitions.
  End implementation partition names in `.impl`; match file stems to the final
  module segment before `.impl`.
- Partition names use the owning responsibility and the file's role. Collapse a
  repeated owner name at its entry.
- Put imports in one block after the module declaration, partitions in lexical
  order and `import std;` last. Import only used dependencies; implementations
  import their contract.
- Shared declarations in compiler, tool, and internal-test partitions use distinct
  names identifying their domain, without named C++ namespaces. Imports control
  visibility; partitions of one named module still share declaration identity.
- Use global module fragments for required platform headers, macro-only test
  headers, or consumer headers exercised by tests.
- Put translation-unit-private declarations in an anonymous namespace.
- Follow each namespace opening with a blank line and close it with a namespace
  comment, including the name for named namespaces. Combine consecutive named
  openings into a qualified definition when the whole body belongs to the innermost
  namespace; preserve inline qualifiers.
- Support components expose contracts through their owning namespaces, which
  supply the domain qualifier for member names.
- Interfaces shared by subsystem headers use a namespace named for their role,
  including helpers serving that responsibility. Header-local helpers may use a
  separate private scope.
- Backend-dependent support types may use an inline namespace: callers use the
  component name; the defining namespace retains the selected backend's type identity.
- Helpers used only by one class belong in its private scope where C++
  template rules permit it.
- Expose operations needed by production callers through the owning interface.
  Published state has one authoritative query surface. Update compiler-private
  interfaces together with their callers.

## Naming

| Kind | Form |
| --- | --- |
| Type, class, enum, enum case | `UpperCamelCase` |
| Function, member, local, parameter, file, directory | `lower_snake_case` |

- Preserve acronyms. ID types end in `ID` and ID collections in `_ids`. Use `id`
  when context identifies the role; otherwise qualify it.
- Use the same domain vocabulary in directories, module partitions, types,
  operations, and tests. Use `decl`, `expr`, and `stmt` for those concepts in
  directory names, module segments, and file stems.
- Use concise names precise in context. Name types and APIs for their roles at use
  sites; omit domain qualifiers supplied by the parent directory and retain those
  distinguishing responsibilities. Update names, callers, and architecture
  documentation when responsibilities change.
- Name builders for results, analyzers for scope, and implementation slices for
  responsibility. Avoid plural long forms merely distinguishing owners from vocabulary.
- Compiler module-name segments are non-keyword C++ identifiers. Do not use the
  bare contextual keyword `module` as a segment; choose a name that states its
  role.
- Protocol names and fixture spellings under test retain their required form.

## Declarations

- In compiler source, declare ordinary member functions in the class and define
  them out of class in `.cpp`, including constructors and single-line accessors.
  Private helper classes in `.cpp` follow the same rule within that file.
- Keep template and `constexpr`/`consteval` definitions visible where needed.
  Keep `= default` and `= delete` in the class declaration.
- Use time traces to find repeated heavy template instantiations. When multiple
  files use one concrete context, put calls behind an ordinary entry in an existing
  implementation file that already instantiates it.
- Keep consecutive single-line member function declarations compact. Separate
  adjacent declarations by one blank line when either spans multiple lines, including
  template heads, attributes, or requires clauses. Preserve attached comments and
  responsibility groups; separate access sections and data members as needed.
- Use trailing return types for named functions except constructors, destructors,
  and conversion functions.
- Mark single-argument constructors `explicit`, except copy and move constructors.
  Use `typename` for template type parameters.
- Mark project-defined classes and structs `final` unless a C++ protocol requires
  inheritance.
- Use `struct` for transparent aggregate records and `class` for types that
  own invariants or a lifecycle.
- Mark every noncapturing lambda in `src/` and `tests/internal/` as `static`.

## Values and initialization

- Use `constexpr` for immutable value locals permitting constant-expression
  initialization, `const auto` for immutable runtime values, and `auto` for values
  modified, moved from, or mutably borrowed.
- Choose storage duration by object lifetime and identity.
- `auto` may deduce pointers; `auto*` is optional. Pointer-value constness is
  independent of pointee constness. Preserve pointee constness in deduction;
  spell the pointer type when narrowing mutable access to the pointee.
- Use designated initializers in declaration order for non-empty project records.
  Initialize every field explicitly, including booleans, empty containers, and
  empty optionals. Use empty braces for fieldless values and braces for container
  literals.
- Transparent record fields have no default member initializers unless a consumed
  C++ protocol cannot be satisfied by explicit construction; document that requirement
  at the declaration. Classes may initialize private execution state in members.
- Give intentionally typed integer literals lowercase suffixes rather than
  constructing fixed-width aliases solely to type them.
- Compiler-owned size literals use `uz`. Consumer-facing source uses deduction
  or an explicitly typed `std::size_t` value.
- Use `static_cast` for explicit conversions it supports.

## Ownership and borrowing

- Use standard value containers for variable-size owned storage.
- Keep borrowed storage alive and valid throughout use. State lifetimes and
  invalidating operations at the owning interface; do not return borrows into
  temporaries or locals that die on return.
- Ordinary task forwarders return the existing task directly. Retain borrowed
  contexts until completion, including across `co_await`.
- Treat moving or consuming an owner as ending its outstanding borrows.
- Construction-time readers return values when subsequent container growth
  could invalidate an exposed borrow.
- Pass trivial values by value. Borrow text with `std::string_view`, read-only
  ranges with `std::span<const T>`, required objects with references, and nullable
  objects with pointers. Do not use output parameters.
- Use `const auto&` for read-only lvalue borrows.

## Failure handling

- Declare project implementation functions `noexcept` unless a consumed C++
  protocol requires a conditional exception specification.
- Defaulted special members use the native exception specification unless an
  explicit protocol boundary requires `noexcept`; deleted members need none.
- Template requirements follow the operations needed by the consumer. A
  `noexcept` template may accept potentially throwing operations when escaping
  exceptions are defined to terminate.
- Do not use `throw`, `try`, or `catch` in compiler code.
- Terminate on failed internal invariants.

## Control flow

- Return early for validation and failure propagation.
- Prefer prefix increment. Use postfix increment only when its previous value
  is required.
- Use `std::unreachable()` after an exhaustive enum switch only when every
  branch transfers control and falling through the switch is impossible.
- Call `std::get` only immediately after checking the active alternative;
  otherwise use `std::get_if`.
- Preserve evaluation order when simplifying expressions.
- Use member `visit` for one variant and `std::visit` for multiple variants.

## Comments

Explain hidden constraints, invariants, or workarounds. Do not retain removed
code or add decorative separators.
