# C++ interoperation

[Language](README.md)

This page defines language behavior at C++ boundaries. Native build requirements
and generated-file integration are covered by the [Toolchain](../toolchain/artifacts.md).

- [Names and lookup](#names-and-lookup)
- [External types and construction](#external-types-and-construction)
- [C string literals](#c-string-literals)
- [External operations and conversions](#external-operations-and-conversions)
- [External access and lifetime](#external-access-and-lifetime)
- [Source fragments](#source-fragments)
- [Declared function boundaries](#declared-function-boundaries)
- [Native exception boundary](#native-exception-boundary)

`import <...>` and `import "..."` include C++ headers. Top-level `#[cpp]`
contributes an implementation source fragment. `import(cpp)` declares a
C++-implemented Carven function; `export(cpp)` publishes a Carven function to
C++ consumers. Source fragments are implementation-only.

Carven has no source operations for creating threads, sharing state between
threads, or synchronizing access. Read/Write/Take and borrow checking do not
establish cross-thread safety. C++ fragment authors, import providers, and export
callers own concurrent calls, shared data, synchronization, and referent lifetimes.

## Names and lookup

A header import makes its header available through C++ inclusion; Carven does
not read its contents or associate declarations with individual headers.
A name with a leading `::` delegates lookup directly to the global C++ namespace:
`::calculate`, `::vendor::calculate`, and the type `::Point` bypass Carven local,
module, and builtin name lookup. Global paths work in expressions, type
annotations, casts, and construction, including external type applications such
as `::std::vector<i32>`. No header import is required for
Carven to admit such a reference; C++ checks whether the declaration is available.
Global references do not mark explicit `using` selections as used.

A header import without `using` creates no Carven names. A `using` selection
introduces external names or a namespace lookup environment in the importing
module:

```carven
import <vector> using std::vector;
import "provider.hpp" using vendor::{ Widget, create };
import "legacy.hpp" using { Point, calculate };
import <vector> using std::*;
```

Explicit selections bind their final name component. Selection paths are rooted
in the global C++ namespace: a single-component selection such as `calculate`
selects `::calculate`, while `vendor::Widget` selects `::vendor::Widget`.
A local explicit name selects one complete external path. Repeated selections
of that same path are allowed; different paths with the same final component
are a catalog error. An explicit selection takes precedence over opened
namespaces. All equivalent selection origins are marked used together.
Further qualified components append to the selected path. Overloads at that
one path remain C++'s responsibility. Cross-path overload merging is not supported;
use explicit global references when selecting different same-named providers.
Namespace selections open a C++ lookup environment without enumerating
declarations.

Local bindings and resolved Carven declarations retain their ordinary lookup
rules. Explicit external imports cannot collide with local module declarations.
Otherwise unresolved ordinary names can use a module's explicitly opened C++
namespaces. External declarations, overloads, template arguments, members and
conversions are checked by C++. External imports are module-local and are not
re-exported through Carven module imports. Explicit selections have unused-import diagnostics;
namespace selections do not.

## External types and construction

Type arguments are accepted only for external C++ names and must be types;
nested external type applications are supported. External construction uses
`T { ... }` with positional initializers, which may be empty. External types in
function signatures and field declarations must be named explicitly; local
owners may infer their type from an external expression. Such results are
not Carven constants and do not participate in pattern coverage or
failure-set construction. C++ determines the result of the complete braced
construction, including template argument deduction and narrowing checks.
Read scalar expressions with established constant results and no selected source
storage deliver those constants to native construction. This includes literals,
named constants, and folded scalar expressions. Their effects and failures still
execute before value delivery. Reading an ordinary binding retains its storage
access and does not establish a native constant expression.
When an earlier aggregate component is saved across a later fallible initializer,
final construction requires copying or moving it from that storage. An immovable
component can therefore fail native compilation after successful Carven analysis.

## C string literals

`c"text"` produces an external `const char*` value pointing to immutable static
storage containing the decoded UTF-8 bytes and a trailing NUL. Empty text is
valid. Internal NUL, including `\0` and `\u{0}`, is rejected. Its type is always
a pointer, including direct native calls and template deduction; it is not a
character array or a Carven `str`. Copies retain access to program-lifetime storage.

C strings support constant initialization, function calls, local copies,
assignment, and Take during static execution. Freezing preserves their
bytes and pointer type, including in supported aggregates. Each emitted pointer
refers to program-lifetime storage; pointer identity across translation units is unspecified.

Printing treats C strings as text: top-level bytes are verbatim, while nested
text is quoted and escaped. Static execution and interpretation read retained
C string bytes for display and default text formatting. Reading unknown native
memory, comparing C string pointers, and
observing their addresses are outside the evaluator's supported operations.
C string literal patterns are rejected.

Native interpolation uses the C++ string formatter. Address formatting requires
an explicit conversion to a pointer type with a pointer formatter, such as
`const void*`. Contextual typing preserves the external `const char*` type;
conversions follow the external conversion rules below.

```carven
import <cstdio> using std::printf;
fn main() { printf(c"Hello World\n"); }
```

## External operations and conversions

When a value must match a destination type, differing types are delegated to
C++ conversion if either is an external C++ type. This applies to annotated
runtime initializers, assignment values, Read and Take call arguments, return
values, aggregate elements, and value-control results with a selected result type.
Carven generates construction of the destination type from the source value;
C++ checks that construction, including narrowing restrictions. No `as` is
required to request this conversion. Matching external type descriptions need
no additional conversion.

Write arguments retain their writable storage instead of constructing a
converted value. When either type is external, C++ checks whether that storage
can bind to the parameter reference. The explicit `&` marker and Carven
writability checks still apply.

An external value used as a condition or an operand of `&&` or `||` is
converted to `bool`. This does not permit an ordinary Carven integer as a condition.
For `value as T`, if either type is external, C++ checks an explicit
`static_cast<T>`. It follows C++ conversion rules rather than the closed
Carven numeric conversion set.

Unary operations and non-logical binary operations involving an external
operand are checked by C++, including overload selection and result typing.
Logical `&&` and `||` retain Carven short-circuit evaluation. A result inferred from
an external expression remains an external type description even when C++
eventually determines a builtin type; Carven does not infer that equivalence
from the provider's declaration.

For example, given a C++ `values.size()` result:

```carven
let count: usize = values.size();
let narrow = values.size() as i32;
```

The first binding requests destination construction; the second requests an
explicit cast. C++ decides whether each conversion is valid.

## External access and lifetime

Carven access rules still apply. Ordinary external-call arguments provide Read
access, `&value` provides Write, and `&&value` takes an owner. Receivers inherit
their storage's access. Local bindings remain owners: initializing one from a
C++ reference result initializes an owned value, subject to C++ construction
rules. External member and index places inherit the root's access; external
indexing has the provider's bounds behavior, not Carven array bounds checks.
External calls do not expose typed failures. An exception escaping a generated
`noexcept` boundary terminates under C++ rules.

Carven checks its own storage availability and explicit access conflicts but
does not infer C++ reference retention, pointer validity or iterator invalidation.
Known callable borrows and Write captures cannot cross an undeclared external
contract. The `ptr` model checks target access and local non-null facts without
proving target liveness. External reference bindings, pointer arithmetic and
external iteration protocols are unsupported.

Explicit C++ calls accept dynamic text views and aggregates containing them.
Carven protects known backing during argument evaluation and the call. The
provider and caller are responsible for retention, returned aliases, reentry,
and indirect pointer lifetimes. Passing a view slot with Write access does not
establish that it stops borrowing its previous backing. Callable-view and
Write-capture escape restrictions also apply to values containing text views.
String has no implicit conversion to a native string container.

Native calls, construction, and representation conversions do not infer borrowed
storage for their results. C++ checks whether delegated operations are valid;
the provider and caller own the result's storage and lifetime contract. Returning
to a Carven type does not establish a previously unknown backing relationship.
Known input borrows remain checked while the operation evaluates.

## Source fragments

Each top-level `#[cpp]` payload remains an independent, byte-opaque C++ source
fragment. Carven does not parse or type-check it, interpolate Carven values, or
bind same-spelled Carven names. C++ owns macros, overloads, templates, linkage,
exceptions, lifetime, ODR, and undefined behavior inside each fragment.
Fragments are implementation-only and never publish declarations to generated
headers. Public Carven entry points use `export(cpp)`; public native declarations
belong in native headers. Fragment fences do not isolate macro or pragma state.
A fragment may contain its own configured includes, but cannot configure headers
already included before it. Distinct native compilation environments belong in
separate C++ source files managed by the build system.

A staged body uses native names published through header imports or
`import(cpp)` declarations. Fragment-only names remain implementation-only.

## Declared function boundaries

An `import(cpp)` declaration is private or bare and has no Carven body:

```carven
private import(cpp) fn native_value(value: i32) -> i32;
```

Calling it invokes the same-named global C++ provider with the declared argument
access. C++ resolves overloads and function templates from that call. Carven does
not generate the provider declaration or parse, import, or compare its C++
signature.

Provider conformance, definition, and link satisfaction are author/toolchain
responsibilities. Same-spelled imports in different modules remain distinct
Carven capabilities even when C++ linkage makes their providers identical.
Names beginning with `_` or `__` receive no additional Carven restriction;
whether such a spelling is reserved in its downstream C++ context is the
author/toolchain's responsibility. `main`, `std`, and `carven` are not valid
provider names.

An `export(cpp)` declaration is an ordinary, complete-compilation-visible
Carven function with a Carven body. It is also declared in the generated C++
API for its module:

```carven
export(cpp) fn value() -> i32 => 42;
```

An `import(cpp)` declaration cannot be exported directly in any syntactic
combination; re-export is not a language feature. A wider capability requires
an explicit ordinary Carven wrapper. Both directions use ordinary function type,
access, visibility, ownership, and declared failure rules. `void` remains
result-only; callable-view escape and
public-surface visibility checks remain ordinary Carven restrictions.

The generated signatures use the same C++ representations as Carven functions:

| Carven contract | C++ representation |
| --- | --- |
| Scalars | Their ordinary scalar types, including `char32_t` for `char` |
| `String` / `str` | `carven::runtime::String` / `std::string_view` |
| Arrays, slices, ranges, pointers | Their ordinary generated container, view, and pointer types |
| Structures, enums, concrete closures | Their generated nominal types |
| Native types | The declared C++ type, with its header environment |
| Callable views | The runtime callable representation for the declared signature |
| Read parameters | The ordinary Read policy: value snapshots or const references |
| Write parameters | Mutable references |
| Take parameters | Owned values; exports use ordinary transfer and imports forward native rvalues |
| Infallible result | The ordinary result type, including `void` |
| Declared failures | `carven::runtime::Outcome<Result, Failures...>` |

The generated API header includes the required generated type definitions,
native header environments, and runtime support. Consumers use that header and
matching runtime headers as a C++ source interface.
Internal test-stop transport is not added to the exported function's declared
failure set. An escaping test stop terminates at its native entry; declared
failures remain observable Outcomes.

Carven checks its side of the contract and generates consistent calls, declarations,
and definitions. C++ checks native call compatibility and the linker resolves
symbols. Native providers and callers own lifetime, retention, reentry, and value
validity obligations, including valid UTF-8 and Unicode scalar values. An imported
result does not establish an unknown backing relationship. Native Write operations
do not prove that previously borrowed backing is released. Declared callback
parameters may be invoked during the call; their declaration does not grant
permission to retain borrowed callable storage beyond its lifetime.

Direct infallible imported `char` results and exported Read/Take `char` parameters
also perform a Unicode scalar check; invalid values terminate. This check is not
recursive validation of aggregates, pointers, mutable references, or Outcomes.

Public module components and exported function names use one deterministic
encoding. Safe C++ identifiers retain their spelling unless they start with
`cv_escaped_`. Other spellings become `cv_escaped_` followed by the lowercase
hexadecimal encoding of their original bytes. Safety excludes the project's
C++ keyword set, double underscores, and an initial underscore followed by an
uppercase letter. Encoding introduces no source-name reservation and depends
on neither compilation order nor other names. Native `import(cpp)` provider
names still denote existing C++ names and are validated without encoding.
A function/namespace prefix collision anywhere in the compilation's public API
tree is invalid.

Module paths determine the nested namespaces under `carven::api`. Directory
names may be C++ keywords: a component named `export` becomes
`cv_escaped_6578706f7274` in C++, while its Carven name remains unchanged.
C++ callers use the names declared in the generated API header. For APIs
intended for handwritten C++ callers, prefer module path components and
exported function names that retain their spelling under this encoding.

## Native exception boundary

C++ exceptions are outside Carven failure sets. Carven does not translate them
into failures or catch them with `try`.

Generated Carven functions, closure call operators, import bridges, and export
façades establish `noexcept` boundaries. External operations need not declare
`noexcept`. An exception escaping one of these boundaries invokes
`std::terminate` under C++ rules. This applies to explicit `import(cpp)` calls
and to header-imported operations, including construction, member and operator
calls, and object lifetime operations.

The integration author owns native exception recovery. To continue Carven
execution after a native exception, C++ code must handle it before it escapes a
`noexcept` boundary. The handler may recover internally or communicate an
application-defined result through the chosen interface. An `import(cpp)`
adapter can return an explicit Outcome for a declared Carven failure contract;
C++ exceptions are never translated automatically.

Native handlers may be defined in headers, linked C++ sources, or top-level
`#[cpp]` fragments under their C++ contracts. Fragments retain their
implementation-only placement and do not enclose generated Carven bodies.
