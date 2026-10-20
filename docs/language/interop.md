# C++ interoperation

This page defines native names, types, operations, function boundaries, and provider
obligations visible to Carven programs.

- [Names and lookup](#names-and-lookup)
- [External types and construction](#external-types-and-construction)
- [C string literals](#c-string-literals)
- [External operations and conversions](#external-operations-and-conversions)
- [External access and lifetime](#external-access-and-lifetime)
- [Execution and initialization](#execution-and-initialization)
- [Source fragments](#source-fragments)
- [Declared function boundaries](#declared-function-boundaries)
- [Provider type support](#provider-type-support)
- [Native exception boundary](#native-exception-boundary)

## Names and lookup

Header imports `import <...>` and `import "..."` make their headers available
through C++ inclusion; Carven does not read their contents or associate
declarations with individual headers.
A name with a leading `::` delegates lookup directly to the global C++ namespace:
`::calculate`, `::vendor::calculate`, and the type `::Point` bypass Carven local,
module, and builtin name lookup. Global paths work in expressions, type
annotations, casts, and construction, including external type applications such
as `::std::vector<i32>`. No header import is required for
Carven to admit such a reference; C++ checks whether the declaration is available.
Global references do not mark explicit `using` selections as used.

Global C++ path components, native header selection paths and list items, and
member components after `.`, `::`, or `->` accept identifier-shaped Carven
keyword tokens. For example, `::vendor::async`, `::vendor::cancel`, and
`handle.cancel()` use native names. Native selection lists may contain keyword
spellings, but a selected keyword does not become a bare Carven identifier.
Use an `as` alias, a global path, or an already available non-keyword prefix to
refer to such a native declaration. Global paths retain the unused-import rules
above. Carven rejects native name spellings in its C++ keyword set with
`CV-CPP-IDENTIFIER`; C++ validates the selected declarations and operations.
This rule does not permit
keyword-named bare Carven declarations or provide escaped identifiers.

A header import without `using` creates no Carven names. A `using` selection
introduces external names or a namespace lookup environment in the importing
module:

```carven
import <vector> using std::vector as Items;
import "provider.hpp" using vendor::{ Widget as Model, create }
import "legacy.hpp" using { Point, calculate }
import <vector> using std::*;
```

List imports end with `}` and have no trailing semicolon. Other header imports
end with `;`, including those without a `using` clause.

Explicit selections bind their final name component unless `as` supplies a local
alias. As with module imports, aliases rename the local binding and preserve the
selected entity. They apply to types, functions, and namespace prefixes.
Selection paths are rooted in the global C++ namespace: a single-component
selection such as `calculate` selects `::calculate`, while `vendor::Widget`
selects `::vendor::Widget`.
A local explicit name selects one complete external path. Repeated selections
of the same path under the same local name are allowed; different paths bound
to the same local name are a catalog error. An explicit selection takes
precedence over opened namespaces. Equivalent selections sharing a local name
are marked used together.
Further qualified components append to the selected path. Overloads at that
one path remain C++'s responsibility. Cross-path overload merging is not supported.
Use distinct aliases or explicit global references for different same-named
providers.
For example, `import "provider.hpp" using vendor::api as native;` lets calls and
types use `native::create()` and `native::Widget` while retaining their complete
C++ paths. An alias introduces only its local name; the original name remains
available for ordinary Carven declarations and bindings. Unaliased selections
retain their final component as the local name.
Namespace selections open a C++ lookup environment without enumerating
declarations.

Local bindings and resolved Carven declarations retain their ordinary lookup
rules. Explicit external imports cannot collide with local module declarations.
Otherwise unresolved ordinary names can use a module's explicitly opened C++
namespaces. External declarations, overloads, template arguments, members and
conversions are checked by C++. External imports are local to their declaring
module. Explicit selections have unused-import diagnostics; namespace selections
do not.

## External types and construction

External C++ type arguments must be types; nested external type applications
are supported. A parameterized Carven function may use its type parameters in
named native applications, such as `::vendor::Box<T>`. Static members use
`::vendor::Box<T>::member`, including overloaded static functions. Construction,
member access, calls, and conversions retain their type and access descriptions
until the Carven function is instantiated. C++ then checks each concrete native
application and operation. Source operations on `T`, including copying and
arithmetic, require capabilities known in the parameterized Carven body.

External construction uses
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
bytes and pointer type, including in supported aggregates. Identical complete
byte contents share immutable program-lifetime backing within one linkage domain,
including across generated modules and artifacts. Copies and repeated literals
therefore preserve the same address. Imported C++ pointers retain their provider's storage contract.
Read arguments of the compiler-known C string type copy the address during
argument evaluation.

Printing treats C strings as text: top-level bytes are verbatim, while nested
text is quoted and escaped. Static execution and interpretation read retained
C string bytes for display and default text formatting. Reading unknown native
memory and directly comparing external C string pointers remain outside the
evaluator's supported operations. A known C string can explicitly convert to
`ptr<void>` for address comparison and formatting with an empty specification or
`p`.
C string literal patterns are rejected.

Native interpolation uses the C++ string formatter. Address formatting requires
an explicit conversion to a pointer type with a pointer formatter, such as
`const void*`. Contextual typing preserves the external `const char*` type.
Erasure to `ptr<void>` preserves the C string address and Read access.

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

Carven has no source operations for creating threads, sharing state between
threads, or synchronizing access. Read/Write/Take and borrow checking do not
establish cross-thread safety. C++ fragment authors, import providers, and export
callers own concurrent calls, shared data, synchronization, and referent lifetimes.

## Execution and initialization

Header imports, native source fragments, and collected C++ sources supply native
implementations. Their presence does not prevent semantic execution of Carven
code. A `const fn` is checked for static capability on its semantically reachable
paths. Interpretation diagnoses unsupported operations when execution reaches them.
Native calls, external object construction, and native data access require an
implementation available to the selected execution stage.

The semantic executor supplies the supported Standard Craft operations directly.
Static capability checking and execution use the same native type and operation
contracts. Public hosting bindings provide additional native operations for
interpreted calls. Native handles may remain local to semantic execution but
cannot be retained as module constants.

Native programs use C++ initialization and destruction rules. Static execution
and interpretation do not load collected native inputs or run their dynamic
initialization and destruction. Their available native providers are already
linked into the compiler host. A program that depends on initialization from
collected C++ inputs requires native execution, even if it makes no explicit native call.

A capability supported across execution stages makes observable initialization
and cleanup part of explicit operations or object lifetimes. Each implementation
supplies those effects in its execution context. Providers and callers satisfy
the native behavior, environment, and lifetime requirements stated by their APIs.
Carven checks the declared boundary; external C++ bodies remain outside its
analysis.

## Source fragments

Native declarations can be supplied by header imports. A source fragment can
define a module-local adapter or include a header that uses generated Carven
signature types.

Each top-level `#[cpp]` payload remains an independent UTF-8 C++ source fragment.
Carven validates its encoding and preserves its bytes without parsing or
type-checking C++ syntax, interpolating Carven values, or binding same-spelled
Carven names. C++ owns macros, overloads, templates, linkage,
exceptions, lifetime, ODR, and undefined behavior inside each fragment.
Fragments are implementation-only and never publish declarations to generated
headers. C++-callable Carven entry points use `export(cpp)`; public native declarations
belong in native headers. Fragment fences do not isolate macro or pragma state.
A fragment may contain its own configured includes, but cannot configure headers
already included before it. Distinct native compilation environments belong in
separate C++ source files managed by the build system.

A staged body uses native names published through header imports or
`import(cpp)` declarations. Fragment-only names remain implementation-only.

## Declared function boundaries

`import(cpp)` declares a function contract whose implementation is supplied
through a C++ interface. `export(cpp)` exposes a Carven implementation through
a C++ interface. Both use signatures written in Carven.

An `import(cpp)` declaration uses ordinary `private`, bare, or `export` source
visibility and has no Carven body:

```carven
private import(cpp) fn native_value(value: i32) -> i32;
```

An optional target binding selects a fully qualified C++ function independently
of the Carven function name:

```carven
private import(cpp) fn native_value(value: i32) -> i32 = ::vendor::compute;
```

The target is a name path rooted at `::`, including namespace or static member
components. It is not an expression and has no explicit template arguments.
Target paths use ordinary native identifier rules and do not select Carven names
or header `using` bindings. With an explicit target, C++ resolves overloads,
function templates, and conversions from the generated call. Carven does not
parse or compare the target's authored C++ signature.

Without a target binding, the canonical source module and function select a
native `invoke` declaration in the generated provider namespace. Its declaration
uses the source signature's types, access, async completion, and failure carrier.
The provider includes the generated header and defines that function. Native
names for this form use the same deterministic encoding as the generated API.

`export import(cpp)` makes the imported function visible to other source domains.
It does not export a second C++ façade. `import(cpp)` and `export(cpp)` cannot be
combined on one declaration.

Provider conformance, definition, and link satisfaction are author/toolchain
responsibilities. Same-spelled imports in different modules remain distinct
Carven capabilities even when C++ linkage makes their providers identical.
Names in explicit targets retain their authored C++ spelling and native
identifier restrictions. Whether a spelling is reserved in its downstream C++
context is the author/toolchain's responsibility.

Ordinary `export` makes a declaration visible throughout the Carven compilation.
`export(cpp)` exposes a function with a Carven body in its module's generated
C++ API. Its Carven visibility remains the ordinary same-craft audience; it does
not grant cross-craft access. It may be synchronous or async:

```carven
struct Value { number: i32 }

export(cpp) fn value() -> Value => { number: 42 };
```

Signature types do not need ordinary `export` merely to appear in a C++ API.
The generated header includes the definitions needed by parameters, results,
and declared failures. Ordinary Carven audience checks still apply: a
module-private type cannot appear in a same-craft function's signature, and a
cross-craft `export fn` still requires correspondingly visible signature types.

Each declaration chooses one public interface: `export` for other crafts, or
`export(cpp)` for C++ callers. These forms do not combine, and `export(cpp)` has
no visibility prefix or list of export destinations. When both audiences need
an API, define their boundaries separately and share ordinary internal logic
where useful. Their contracts may differ; do not add forwarding declarations
merely to mirror every internal function at both boundaries.

Use an ordinary Carven wrapper when adapting an imported capability's behavior.
Both directions use ordinary function type,
access, visibility, ownership, and declared failure rules. Every `import(cpp)`
contract admits a call-level stop. Target bindings select implementations without
changing this contract; declared failures remain separate from stopping. Carven
propagates a stop through callers without entering source `catch` handlers.
Unit values in parameters and aggregates use `carven::runtime::Void`; direct
synchronous unit results use native C++ `void`. Result carriers keep `void` as
their success type. Callable-view escape and public-surface
visibility checks remain ordinary Carven restrictions.

The generated signatures use the same C++ representations as Carven functions:

| Carven contract | C++ representation |
| --- | --- |
| Scalars | Their ordinary scalar types, including `char32_t` for `char` |
| `String` / `str` | `carven::runtime::String` / `std::string_view` |
| Arrays, tuples, slices, ranges, pointers | Their ordinary generated container, product, view, and pointer types |
| Unit values in parameters and aggregates | `carven::runtime::Void` |
| Structures, enums, concrete closures | Their generated nominal types |
| Native types | The declared C++ type, with its header environment |
| Callable views | The runtime callable representation for the declared signature |
| Read parameters | The ordinary Read policy: value snapshots or const references |
| Write parameters | Mutable references |
| Take parameters | Owned values; exports use ordinary transfer and imports forward native rvalues |
| Synchronous result without failure or stop effects | The ordinary result type, including `void` |
| Synchronous result admitting failures or stops | `carven::runtime::Outcome<Result, Failures...>` |
| Async completion contract | Cold `carven::runtime::execution::AsyncOperation<Result, Failures...>` |

Read uses `const T&` for owned Carven storage, closures, and parameter holders
retained across the call boundary, and const values for other pure Carven types.
Other native-containing Read parameters use `carven::runtime::ReadArg<T>`, which
selects a const value for trivial copy construction and destruction, and a const
reference otherwise. The native callee's own signature still controls its C++
parameter initialization. Async providers must capture
Carven Read values passed by value; native-containing Read holders remain
borrowed until the cold operation closes.

The C++20 execution runtime guarantees coroutine frame alignment of at least
32 bytes and the platform's fundamental alignment. Native values and temporaries
retained across suspension must fit that alignment, including those constructed
inside Carven async functions. Keep objects requiring stronger alignment in
separately allocated storage and retain a suitable owner or borrow in the frame;
the ordinary lifetime obligations still apply.

The generated API header includes the required generated type definitions,
native header environments, and runtime support. Consumers use that header and
matching runtime headers as a C++ source interface.
Each exported callable also publishes its source contract under
`carven::api::<encoded module>::cv_escaped_functions::<encoded function>`, using
the same aliases, source identity, and `service` format as provider contracts.
Synchronous functions that may stop return `Outcome<Result, Failures...>`,
including `Outcome<Result>` when there are no declared failures. The stopped
state is separate from declared failures. Exported signatures retain this state;
native callers inspect `is_stopped()` before projecting a result. Async functions
carry it in `Completion`.

Carven checks its side of the contract and generates consistent calls, declarations,
and definitions. C++ checks native call compatibility and the linker resolves
symbols. Carven does not inspect external C++ implementations; providers must
satisfy their declared contract, including propagating a callback's stopped result.
C++ callers initialize generated API parameters under C++ rules, including explicit
`std::move` when transferring a named owner.
Native providers and callers own lifetime, retention, reentry, and value
validity obligations, including valid UTF-8 and Unicode scalar values. An imported
result does not establish an unknown backing relationship. Native Write operations
do not prove that previously borrowed backing is released. Declared callback
parameters may be invoked during the call; their declaration does not grant
permission to retain borrowed callable storage beyond its lifetime.

Successful imported `char` results and exported Read/Take `char` parameters
also perform a Unicode scalar check; invalid values terminate. This check is not
recursive validation of aggregates, pointers, mutable references, or Outcomes.

Public module components and exported function names use one deterministic
encoding. Safe C++ identifiers retain their spelling unless they start with
`cv_escaped_`. Other spellings become `cv_escaped_` followed by the lowercase
hexadecimal encoding of their original bytes. Safety excludes the project's
C++ keyword set, double underscores, and an initial underscore followed by an
uppercase letter. Encoding introduces no source-name reservation and depends
on neither compilation order nor other names. Explicit `import(cpp)` targets
denote existing C++ names and are validated without encoding.
A function/namespace prefix collision anywhere in the compilation's public API
tree is invalid.

Module paths determine the nested namespaces under `carven::api`. Directory
names may be C++ keywords: a component named `export` becomes
`cv_escaped_6578706f7274` in C++, while its Carven name remains unchanged.
C++ callers use the names declared in the generated API header. For APIs
intended for handwritten C++ callers, prefer module path components and
exported function names that retain their spelling under this encoding.

## Provider type support

Every source module containing concrete `import(cpp)` declarations produces
`carven/providers/<canonical/module>.hpp`. The header supplies signature type
aliases and their complete definition dependencies, including module-private
nominal types. Source visibility remains governed by ordinary Carven rules.

Aliases live under
`carven::providers::<encoded module>::cv_escaped_functions::<encoded function>`.

`Result` names the source result representation, `Completion` its mapped native
return carrier, `Parameter0`, `Parameter1`, and so on the source parameter value
types, and `Failure0`, `Failure1`, and so on the canonical failure types.
`Argument0`, `Argument1`, and so on name the exact generated native parameter
types, including references. Explicit native targets retain their authored
parameter initialization. `void` has `Result = carven::runtime::Void`;
a synchronous imported unit result has `Completion = Outcome<void, Failures...>`.
Async Completion names the cold AsyncOperation carrier. Failure numbering follows
canonical failure-carrier order. Review these aliases when changing the source signature.

Include the header in a provider source file or `#[cpp]` fragment when those
types are needed. For declarations without a target, it supplies the exact
`invoke` declaration through the generated interface. Explicit targets retain
their authored header or fragment declarations.

For the `native_value` declaration in canonical module `host`, its implementation
can use the generated aliases directly:

```cpp
#include <carven/providers/host.hpp>

namespace carven::providers::host::cv_escaped_functions::native_value {
auto invoke(Argument0 value) noexcept -> Completion {
    return Completion::success(value + 1);
}
}
```

The same callable namespace publishes `service`, a typed host contract generated
from the source signature, including each parameter's Read, Write, or Take
access. Its name is the canonical source module followed by
`.` and the function name. `source_module` and `source_name` retain those source
components. Hosts bind this contract when supplying the capability to interpreted
or hosted native execution; ordinary native calls remain direct provider calls.

Non-generic structs reachable through provider or exported signatures publish ADL
`carven_type_name(Record*)` and `carven_fields(Record*)` in their owning interface.
The former returns the canonical source type name; the latter returns field names
and member pointers in source order. Host mapping derives field types from those
members, including nested records, vectors, and registered native leaves. Contracts
retain native source names separately from their C++ carrier types. These functions
need no object.

Type aliases refer to the same nominal definitions and field access used by Carven.
They define a C++ source interface; binary compatibility across toolchains is not
specified. A native header defining a type used by the signature must not include
the generated provider header that depends on it.

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
