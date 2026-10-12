# C++ Conventions

These rules apply to project-authored C++ in `src/`, `tests/`, `crafts/`, `benchmarks/`, and `tools/`.
Vendored source and fixtures that preserve an external interface follow their
owning format. Generated artifacts follow the C++ baseline below; the remaining
source-layout and style rules apply to handwritten code. `.clang-format` owns
formatting; neighboring code demonstrates local idioms.

## C++ baseline

- Source in `src/`, `tests/internal/`, and `tools/` may use C++26.
- Crafts, installed support source, generated code, and baseline consumer
  fixtures and native benchmarks must compile with C++20. Implementations may
  select newer facilities when the consumer's standard and library support them,
  while preserving the Carven operation's semantics and functionality.
  Prefer library feature detection for library capabilities.
- The consumer project selects its C++ standard. Optional implementations do
  not raise the required baseline. Explicit newer-standard interop targets may
  exercise APIs from that standard.

## Source layout

- An owner directory contains files for one responsibility. A branch directory
  contains subdirectories. Split owners only into independently understandable
  responsibilities.
- Use directory levels to express ownership and dependency boundaries. Keep an
  owner's entry, state, and implementation slices in the same directory.
- Mixed file and directory layouts are permitted at the repository root, the
  `src/` compiler entry boundary, and where required by build metadata,
  test-group harnesses, or fixtures whose layout is under test.
- Place a contract and its implementation together. A contract may have
  multiple implementation slices within the same owner directory.
- Split files by coherent operations, algorithms, or contracts. Define state
  ownership and dependency direction across implementation slices.

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
| `io/` | Standard-output and standard-error printing |
| `numeric/` | Integer operations and arithmetic checks |
| `simd/` | Vector and mask operations with native and portable backends |
| `testing/` | Inline-test execution and reporting contexts |
| `text/` | String storage, borrowed text views, and UTF-8 operations |
| `value/` | Value representations, result storage, and stateless values |

Expose self-contained contract headers within each domain, such as
`collection/array.hpp` and `format/writer.hpp`. Include the contracts used by
generated code and native consumers. Use a domain entry such as `text/text.hpp`
for implementation headers that share one contract. Keep implementation headers
internal to their domain and runtime includes out of compiler source in `src/`.

Use angle brackets and the complete `carven/` path for Carven headers,
including dependencies within the same domain. Use the corresponding native
import in Carven source. Add the Crafts root to the include search directories:

```cpp
#include <carven/runtime/collection/array.hpp>
#include <carven/runtime/text/string.hpp>
```

```carven
import <carven/std/time/native.hpp>;
```

### Header order

Order independent C++ includes in these categories, with lexical path order
within each category:

1. The implementation's associated interface header, when one exists.
2. Carven runtime headers.
3. Other generated Carven API and runner headers.
4. Other project and craft headers, then third-party headers.
5. C++ standard-library headers, then platform headers.

Make each public header supply its own dependencies.
By default, keep independent Carven-owned headers in one block and separate
external library headers with a blank line. Use one blank line to separate
authored groups when needed, including runtime and generated API headers.
Preserve the order required by macro configuration, conditional compilation,
and implementation-header dependencies. Generated artifacts place explicit
native import environments after the associated header and before synthesized
dependency groups; those environments retain source order. Formatters preserve
include order.

## Modules and visibility

- `src/carven.cppm` is the compiler's primary interface. Other compiler module
  units are partitions.
- Use `.cppm` for contract partitions and `.cpp` for implementation partitions.
  Implementation partition names end in `.impl`. File stems match the final
  module segment before `.impl`.
- Partition names use the owning responsibility and the file's role. Collapse a
  repeated owner name at its entry.
- Put imports in one block after the module declaration. Order partitions
  lexically and put `import std;` last. Import only dependencies used by the
  unit. An implementation partition imports its contract.
- Shared declarations in compiler, tool, and internal-test partitions use
  names that identify their domain without named C++ namespaces. Imports control
  visibility; partitions of the same named module still share declaration
  identity. Keep shared names distinct.
- Use a global module fragment for macro-only test headers, required platform
  headers, or consumer headers exercised by tests.
- Put translation-unit-private declarations in an anonymous namespace. Close
  every namespace with a namespace comment, using the namespace name for named
  namespaces.
- A support component exposes the contract used by generated code and craft
  support sources through its owning namespace. A subsystem namespace supplies
  the domain qualifier for its member names.
- An internal interface shared by headers of one subsystem uses a namespace
  named for its role. Keep implementation helpers there when they serve that
  same responsibility. Header-local helpers may use a separate private scope.
- Backend-dependent support types may live in an inline namespace within their
  component. Callers use the component name; the defining namespace retains the
  selected backend's type identity.
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

- Preserve acronyms.
- ID type names end in `ID`. Use `id` when context identifies its role;
  otherwise qualify the role. ID collections use `_ids`.
- Use the same domain vocabulary in directories, module partitions, types,
  operations, and tests. Use `decl`, `expr`, and `stmt` for those concepts in
  directory names, module segments, and file stems.
- Use concise names that are precise in context. Omit domain qualifiers already
  supplied by the parent directory. Retain qualifiers needed to distinguish
  responsibilities.
- Name types and APIs for their roles at use sites. Update names, callers, and
  architecture documentation when responsibilities change.
- Name builders for their results and analyzers for their scope. Name
  implementation slices for their responsibility. Do not introduce plural long
  forms merely to distinguish an owner from its vocabulary.
- Avoid catch-all directory names; each owner names a coherent responsibility.
- Compiler module-name segments are non-keyword C++ identifiers. Do not use the
  bare contextual keyword `module` as a segment; choose a name that states its
  role.
- Protocol names and fixture spellings under test retain their required form.

## Declarations

- In compiler source, declare ordinary member functions in the class and define
  them out of class in `.cpp`, including constructors and single-line accessors.
  Private helper classes in `.cpp` follow the same rule in that file.
- Keep template and `constexpr`/`consteval` definitions visible where needed.
  Keep `= default` and `= delete` in the class declaration.
- Use compiler time traces to identify repeated heavy template instantiations.
  When several implementation files use the same concrete context, place its
  calls behind an ordinary entry in an existing implementation file that already
  instantiates that context.
- Keep consecutive single-line member function declarations compact within
  each group. Separate adjacent declarations with one blank line when either is
  multiline, including template heads, attributes, and requires clauses.
  Keep comments with their declarations and preserve documented responsibility
  groups. Separate access sections and data members as needed.
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
  records. Explicitly initialize every field at each construction site, including
  booleans, empty containers, and empty optionals. Use empty braces for fieldless
  values and braces for container literals.
- Declare transparent record fields without default member initializers.
  An exception requires a consumed C++ protocol that cannot be satisfied through
  explicit construction; document that requirement at the declaration.
  Classes may initialize their private execution state in members.
- When an integer literal's type is intentional, use a lowercase literal suffix
  instead of constructing a fixed-width alias solely to type the literal.
- Compiler-owned size literals use `uz`. Consumer-facing source uses deduction
  or an explicitly typed `std::size_t` value.
- Use `static_cast` for explicit conversions it supports.

## Ownership and borrowing

- Variable-size owned storage uses standard value containers.
- Borrowed storage remains alive and valid throughout every use. State borrow
  lifetimes and invalidating operations at the owning interface. Do not return
  borrows into temporaries or local storage that dies on return.
- Ordinary task forwarders return the existing task directly. Callers retain
  borrowed contexts until the task completes, including across `co_await`.
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
- Use member `visit` for one variant and `std::visit` for multiple variants.

## Tests

Test names identify a stable owning component and the observed behavior. Name
inputs before iterating and preserve their identity in assertion context. Use
comparison assertions for printable values and guard premises before dependent
observations. Tests establish accepted behavior, rejected inputs, and invariants
at their owning boundaries. Test contracts admit equivalent implementations that
preserve the observed property. Neighboring suites demonstrate registration and
assertion syntax.

## Comments

Explain hidden constraints, invariants, or workarounds. Do not retain removed
code or add decorative separators.
