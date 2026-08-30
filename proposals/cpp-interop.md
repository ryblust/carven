# C++ 互操作契约

- **Status:** Accepted
- **Implementation:** Not started
- **Scope:** C++ companion source、header dependency、`import(cpp)` 与 `export(cpp)` 的显式 source-level contract
- **Depends on:** Current canonical module/declaration/type identities

## Summary

本文统一 Carven 与 C++ 的两个方向，同时拒绝把 ordinary generated C++ 偶然提升为
interop contract：

- `import(cpp)` 声明由严格 C++ adapter 实现的 Carven 函数；
- `export(cpp)` 为 C++ caller 生成显式、稳定的 source-level façade；
- top-level `#[cpp]` 是当前 `.cv` 模块附带的 opaque C++ companion source；
- `import <...>` 与 `import "..."` 声明 companion 的 structured header dependency。

V1 只允许 concrete、infallible、Read/by-value scalar functions。C++ overload、template、
namespace、conversion、exception handling 与 vendor-specific complexity 留在 adapter header/source
或 `#[cpp]`；它们不参与 Carven lookup、inference、generic solving 或 declaration identity。

| Slice | Maturity | Current frontier |
| --- | --- | --- |
| Form syntax and companion model | Accepted | `CPP-01` through `CPP-05` |
| `import(cpp)` scalar V1 | Accepted | `CPP-06` through `CPP-13` |
| `export(cpp)` scalar V1 | Accepted | `CPP-14` through `CPP-18` |
| Generated placement and build boundary | Accepted | `CPP-19` through `CPP-21` |
| Adapter declaration visibility and owning bridge | Accepted | `CPP-24`, `CPP-27` |
| Bodyless semantic integration | Accepted | `CPP-28` |
| Cross-module raw adapter identity | Accepted | `CPP-25` |
| Structured header resolution | Accepted | `CPP-26` |
| Broader values, effects, ABI, and C++ mapping | Deferred | `DEFER-CPP-01` through `DEFER-CPP-09` |

## Context

### Current repository facts

Current Carven accepts top-level, statement, and expression `#[cpp]` regions. A contextual expression
may adopt an expected Carven result type; an untyped expression may carry the internal unspellable
`Foreign` type. Carven does not parse or prove the C++ body. The downstream C++ compiler determines
whether its names, overloads, templates, conversions, and generated representation are valid.

Functions, structures, enums, and module constants currently share one Carven module namespace.
Duplicate declarations are invalid and function overloading is not supported.

Top-level regions already lower into the generated module `.cpp` preamble at global C++ scope, before
the private generated namespaces. Multiple regions preserve source order. Ordinary generated namespace
names, hashes, component headers, helpers, representations, and formatting are private compatibility
details.

A compilation has canonical module paths and a closed module catalog. Current Carven visibility is:

| Source form | Carven audience |
| --- | --- |
| `private` | Defining module |
| bare declaration | Defining module domain |
| `export` | Complete compilation |

The compiler emits C++ source/header artifacts only. Xmake or another downstream build compiles them
into an object, static library, shared library, or binary.

### Problem

Contextual typed `#[cpp]` makes the target compiler accept a conversion, but it does not define a
complete input/output function contract, unique adapter identity, exception boundary, ownership,
lifetime, public name, or artifact. Conversely, ordinary generated C++ is not a stable handwritten-C++
API.

Carven needs one small explicit boundary whose valid Carven facts close before target generation while
still using the C++ backend and toolchain directly.

## Goals and non-goals

### Goals

- Keep ordinary Carven declarations and calls free of C++ type and ABI syntax.
- Give `import(cpp)` one exact, concrete adapter contract.
- Give `export(cpp)` one stable handwritten-C++ header and namespace contract.
- Preserve Carven visibility, evaluation, scalar semantics, and Unicode validity.
- Keep arbitrary C++ expressiveness inside companion/header/source code rather than Carven inference.
- Diagnose every statically knowable Carven representability error before target generation.
- Leave header contents, C++ function bodies, definitions, ODR, and link satisfaction to the C++ author
  and downstream toolchain where Carven cannot know them.

### Non-goals

- A complete C++ header/AST importer or direct arbitrary overload/template binding.
- Stable ABI, cross-compiler binary compatibility, shared-library visibility, or binary-only distribution.
- Pointer, reference, owner, borrow, nullable, string-view, callback, async, or thread contracts.
- Struct, enum, class, array, callable, generic, or failure carriers in V1.
- Automatic C++ exception translation.
- Rename, namespace selection, alternate signatures, façade bodies, or module-wide interop switches.
- A general attribute system or user-defined declaration forms.

## Design

### Head-owned forms

**Maturity:** Accepted.

The common syntax mechanism is `head(form)`. The head owns and interprets its contextual form name:

```carven
class(interface) Printer { /* class proposal owns semantics */ }

import(cpp) fn native_value() -> i32;

export(cpp) fn value() -> i32 {
    return 42;
}
```

The forms are scoped conceptually as `class::interface`, `import::cpp`, and `export::cpp`; they are not
global annotations. Bare heads retain their ordinary/default meaning. An unknown, empty, or multi-value
form is rejected and never falls back to the bare head.

`@` remains unassigned. A future `@annotation` must be orthogonal metadata that does not change a
declaration's kind, implementation authority, audience, ABI, or body language. `#[cpp]` remains a
source-language fence rather than an attribute.

### C++ companion source

**Maturity:** Accepted.

A top-level `#[cpp]` region contributes opaque bytes to the defining module's one logical C++ companion
source:

```carven
#[cpp] {
    namespace {
        auto helper(std::int32_t value) noexcept -> std::int32_t {
            return value + 1;
        }
    }
}
```

Multiple physical regions are allowed and retain their source order. Structured header imports precede
the logical companion. The companion is emitted at global translation-unit scope before generated
Carven namespaces. Carven does not wrap it in a named or anonymous namespace, parse it, rewrite names,
or infer declarations from it.

The author therefore owns preprocessor state, C++ helper linkage, namespace placement, templates,
specializations, overloads, exception handling, lifetime, undefined behavior, and all other raw C++
effects. Names actually reserved for compiler-generated integration remain unavailable; Carven does not
otherwise enforce C++ style or portability rules such as banning `_name` or `name__part`.

V1 removes expression- and statement-position `#[cpp]` together with the `Foreign` path. Code inside a
Carven function reaches C++ only through a declared `import(cpp)` adapter.

### Structured header dependencies

**Maturity:** Accepted.

Header imports join ordinary module imports in the source import prefix:

```carven
import <vendor/api.hpp>;
import "native/adapter.hpp";
import model.value using Value;
```

- `<...>` preserves its header-name spelling and lowers to the corresponding C++ `#include <...>`.
- `"..."` preserves its header-name spelling and lowers to the corresponding C++ `#include "..."`.
- Both forms use the downstream C++ preprocessor's ordinary header-search rules and configured include
  paths. Carven does not resolve them relative to the owning `.cv` source, reinterpret them relative to
  a virtual-source display origin, or promise independence from the generated translation unit's
  location.
- The contents use a dedicated header-name lexical form, not an ordinary escaped Carven string literal:
  there is no Carven escape processing or interpolation, and the matching delimiter or a newline cannot
  occur within the header name.
- Header imports are module-local and visible to the complete logical companion regardless of their
  position within the import prefix.
- They do not create Carven declarations, import C++ names into Carven lookup, or link a library.
- An imported header may declare or define a strict adapter that satisfies `import(cpp)`.
- Preprocessor-sensitive, repeated, or specially ordered includes may instead be written directly in
  `#[cpp]`; their behavior is then wholly the boundary author's responsibility.

### `import(cpp)` is a Carven declaration with a C++ implementation

**Maturity:** Accepted.

`import(cpp)` is an ordinary top-level function declaration, not an import-prefix item and not a C++
prototype written in Carven syntax:

```carven
private import(cpp) fn module_adapter(value: i32) -> i32;
import(cpp) fn domain_adapter(value: i32) -> i32;
export import(cpp) fn compilation_adapter(value: i32) -> i32;
```

`private`, bare, and `export` keep their current module, module-domain, and complete-compilation
audiences. Once declared, an imported function is called like any ordinary Carven function; the trust
boundary is the declaration and its provider, not every call site.

The Carven declaration contains only Carven names, parameter access, types, result, and effects. It has
no `noexcept`, C++ type, link-name string, namespace path, `from`, overload, or template syntax.

#### Semantic implementation origin

A function's canonical identity, signature, parameters, visibility, effects, and callable type are
independent of where its implementation comes from. Semantic IR models that origin explicitly:

```text
FunctionImplementation =
    CarvenBody(BodyID)
  | CppImport(CppImportID)
```

`CppImportID` identifies the owner-local C++ binding and bridge facts; it is not a `BodyID`. Signature and
parameter facts needed by calls, callable-view adoption, diagnostics, and target declarations therefore
cannot live exclusively inside a Carven body.

Every pass that consumes function implementations handles the variants deliberately. Reference, effect,
availability, control-flow, and body-contract analyses do not traverse a nonexistent C++ import body;
interop-specific representability and bridge validation consume `CppImport` facts instead. No synthetic
ordinary Carven body is created to smuggle the declaration through body-oriented pipelines.

#### Adapter name and declaration

The V1 adapter entry is the unique function found by the qualified C++ lookup expression `::name`, with
the exact same spelling as the Carven function. This includes a declaration introduced into global
lookup by a directly nested unnamed namespace; it does not claim that such a declaration is itself a
member of the global namespace. The spelling must be accepted as a function identifier by every
supported generated-C++ dialect. C++ keywords and alternative tokens such as `new`, `delete`, `nullptr`,
`and`, and `or` are rejected by Carven. Carven also rejects lexically valid names that are known not to
form this boundary: `main` has special entry-point and address rules, while `std` and `carven` collide
with required global namespaces. Lexically valid implementation-reserved spellings involving `_` or
`__` are not rejected merely as policy; their use remains the author's responsibility.

Carven does not synthesize a C++ declaration for the adapter. The programmer first makes the adapter
declaration or definition visible to the owning module through a structured header or the module's
companion. The conceptual order below is a **Carven-generated module `.cpp` artifact**, not a file the
programmer must write:

```cpp
// Compiler-owned runtime, generated interface, and carrier headers.
#include <cstddef>
#include <cstdint>
#include <type_traits>

// Structured user header import, if present.
#include "native/adapter.hpp"

// Exact bytes from the module's top-level #[cpp] companion regions.

namespace carven::generated:: /* owning module identity */ {
    // Materialized example for an import(cpp) declaration.
    auto native_value_bridge(std::int32_t value) noexcept -> std::int32_t {
        auto adapter = &::native_value;
        static_assert(std::is_same_v<
            decltype(adapter),
            std::int32_t (*)(std::int32_t) noexcept
        >);
        return adapter(value);
    }
}
```

The evaluated, untargeted `auto adapter = &::native_value` requires one visible, uniquely addressable
function without selecting from an overload or template set. Comparing the deduced pointer type then
requires the exact parameters, result, and `noexcept`. It also prevents an immediate `consteval`
function from serving as a runtime adapter. A deleted, missing, overloaded, or mismatched function is a
downstream C++ error attributed to the owning `import(cpp)` declaration.

The check deliberately does not require one storage or language linkage. An exact `static`, unnamed-
namespace, ordinary external, `extern "C"`, `inline`, or header-defined `constexpr` function is callable
through the owning bridge and may satisfy the boundary. Those details remain in programmer-owned C++ and
do not become Carven declaration identity or ABI promises.

The function may therefore be defined by:

1. an imported inline adapter header;
2. a linked C++ object/library whose header declares the adapter; or
3. the current module's `#[cpp]` companion.

A linked object without any visible declaration is not a special supported mode. Its author supplies a
normal header or writes a declaration in the companion, just as ordinary C++ requires.

Vendor APIs in namespaces, overload sets, templates, or incompatible signatures require a distinct,
unambiguous wrapper function visible in the owning translation unit. V1 provides no direct namespace or
rename mapping.

After validation, every Carven call crosses the materialized owning bridge and never names the raw
adapter. `private`, bare, and `export` affect that bridge's Carven audience and physical linkage, not the
identity used by direct calls or callable-view adoption. Satisfying any required external definition
remains a downstream build/link responsibility.

### Adapter identity and owning bridge

**Maturity:** Accepted.

An `import(cpp)` declaration has its ordinary canonical Carven identity: owning module plus declaration
name. Its owning module owns one logical bridge: the raw binding, exact carrier contract, validation,
conversion, and visibility boundary. V1 materializes that bridge exactly once in the owner module.

Every `private`, bare, or `export` import therefore has one physical owning bridge. A private bridge has
internal placement and may be inlined by the target optimizer.
This gives direct calls and callable-view adoption the same function identity and guarantees that a
returned `char32_t` cannot bypass Unicode validation. Bridge elision is not part of the source contract;
any future optimization must be observationally equivalent, call the validated binding rather than
respelling `::name(args)` under macro expansion, and preserve carrier conversion, Unicode validation,
evaluation order, and callable identity. Other Carven modules call only the owning interface and never
need the C++ adapter header, companion, raw name, or provider linkage.

Consequently, C++ lookup and exact validation occur only in the owner `.cpp`. This preserves structured
headers as module-local dependencies, keeps their macros and include conditions out of caller
translation units, and centralizes scalar conversion and Unicode validation.

Today's lack of function overloading still applies inside each Carven module. Within the owner C++
translation unit, the raw qualified lookup expression must also resolve to one exact addressable
function; the untargeted address check rejects a visible overload or template set.

Raw adapter binding is module-local. Two modules may independently declare same-spelled `import(cpp)`
functions with equal or different contracts and include different headers. Their canonical Carven
identities and owning bridges remain distinct, and Carven neither unifies nor rejects them. Whether the
raw declarations denote distinct or identical C++ entities is determined solely by C++ linkage, ODR,
headers, objects, and libraries; equal raw spellings do not prove a shared provider or independent
entities.

This is deliberately separate from consumer lookup. Given valid declarations `image.open` and
`audio.open`, a consumer that writes both `import image using open;` and `import audio using open;` tries
to introduce two declarations into one unqualified Carven name and is rejected by ordinary import/name
conflict rules. Both producer declarations remain valid. Carven currently has no import alias form to
make that particular unqualified composition usable; that usability limitation does not invalidate the
underlying module capabilities or create an FFI collision.

### Scalar carrier closure

**Maturity:** Accepted.

`CppCarrier(T)` is a deliberate interop contract distinct from private ordinary lowering, even where
their current spellings coincide:

| Carven type | C++ carrier | Boundary rule |
| --- | --- | --- |
| `bool` | `bool` | Direct C++ source-level value |
| `i8/i16/i32/i64` | `std::int8_t/std::int16_t/std::int32_t/std::int64_t` | Exact type |
| `u8/u16/u32/u64` | `std::uint8_t/std::uint16_t/std::uint32_t/std::uint64_t` | Exact type |
| `isize` | `std::ptrdiff_t` | Supported native data model |
| `usize` | `std::size_t` | Supported native data model |
| `f32` | `float` | Supported IEEE binary32 model |
| `f64` | `double` | Supported IEEE binary64 model |
| `char` | `char32_t` | Validate values entering Carven |
| `void` | `void` | Result only |
| `str` | Unsupported | UTF-8 and backing lifetime are not closed |

C++ aliases are accepted when they denote the same C++ type; equal width alone is insufficient.
`bool` deliberately remains `bool` because `cpp` denotes a same-toolchain C++ source contract. A future
C ABI form may choose a different boolean carrier.

V1 functions must be concrete, non-generic, fixed-arity, and infallible. Every parameter is unmarked
Read access and crosses by value. Write (`&`), Take (`&&`), failure sets, methods, arrays, nominal types,
callables, `str`, and Carven default/variadic boundary syntax are rejected. A fixed-arity C++ provider
may carry default arguments because they are not part of its function type; the generated bridge always
passes every argument, so those defaults are accepted but never used. A variadic C++ function does not
match the exact fixed-arity pointer type.

For `import(cpp)`, a Carven `char` argument is already valid; a returned `char32_t` is checked before it
becomes a Carven `char`. An invalid scalar is a boundary contract violation and terminates rather than
creating a recoverable failure.

### Exception and failure boundary

**Maturity:** Accepted.

Every imported adapter declaration is `noexcept`, but `noexcept` is not Carven source syntax. A companion
definition must explicitly match it. Carven does not generate `catch (...)` or infer an error protocol.
Throwing through the `noexcept` adapter terminates according to C++ rules.

An adapter that calls throwing vendor code must catch and explicitly encode the outcome within the V1
scalar signature. Formal typed-failure and C++ exception mapping is deferred.

### `export(cpp)` audience and source closure

**Maturity:** Accepted.

`export(cpp)` monotonically extends ordinary Carven visibility:

| Source form | Audience |
| --- | --- |
| `private fn` | Defining module |
| bare `fn` | Defining module domain |
| `export fn` | Complete compilation |
| `export(cpp) fn` | Complete compilation and C++ consumers |

There is no C++-visible but Carven-hidden state. `export(cpp)` selects the same declaration identity and
does not create a second Carven façade declaration.

An ordinary `export import(cpp) fn` is allowed: it exposes a C++-implemented function throughout the
Carven compilation. `export(cpp) import(cpp)` is rejected as an explicit V1 scope limit. Such a
composition could expose a private-linkage or C-linkage provider through a stable namespaced façade, so
it is not semantically impossible; an explicit ordinary Carven wrapper selected with `export(cpp)` is
the supported V1 spelling.

V1 accepts only top-level functions satisfying the same scalar closure. Structs, enums, classes,
constants, methods, generics, Write/Take parameters, and nonempty failure sets are rejected.

### C++ consumer surface

**Maturity:** Accepted.

For canonical module path `image.codec`:

```carven
export(cpp) fn decoded_size(width: u32, height: u32) -> usize {
    // ...
}
```

Carven emits a standalone public header at:

```text
carven/api/image/codec.hpp
```

with the stable C++ namespace and declaration:

```cpp
namespace carven::api::image::codec {
    auto decoded_size(std::uint32_t, std::uint32_t) noexcept -> std::size_t;
}
```

One public header is emitted for each module containing at least one `export(cpp)` declaration. Header
path and nested namespace components follow the canonical module path. Function names retain their
Carven spelling. A selected module path component or function name that is not a valid supported C++
identifier is rejected by Carven.

The complete generated public surface must also be a valid C++ namespace tree. For example, module
`a` cannot export a C++ function named `b` while module `a.b` (or any descendant) emits a public C++
declaration: `carven::api::a::b` would need to be both a function and a namespace. Carven rejects the
first such prefix-surface collision before lowering and identifies both source declarations.

The public header is self-contained, repeat-includable through an include guard or equivalent mechanism,
and independently compilable under every supported C++ dialect. It directly includes only the standard
declarations required for the carriers it spells and never relies on transitive includes. It does not
expose or include private `carven::generated` names, linkage hashes, SCC component headers, runtime
carriers, or ordinary lowering representations. Parameter names, if emitted, are formatting only and
are not part of the stable source contract.

The outward adapter is generated automatically with external C++ linkage and `noexcept`. It validates
a C++ `char32_t` argument before entering Carven; Carven results are already valid. Invalid Unicode is a
terminating boundary violation. No C++ exception or automatic error carrier is produced.

### Generated placement and linkage

**Maturity:** Accepted.

Conceptually, a generated module implementation uses the following emission and ownership order:

```cpp
// 1. Compiler-owned runtime and standard carrier headers.
// 2. Own and dependency generated/SCC interface headers.
// 3. Structured user headers.
// 4. Raw global companion source.

namespace carven::generated:: /* private linkage and module namespaces */ {
    namespace {
        // 5. private import(cpp) bridge materializations,
        //    private Carven functions,
        //    and private struct/class/enum declarations
        //    compiler-owned module-local helpers/materializations with runtime identity
    }

    // 6. bare/export import(cpp) bridges and other Carven entities
    //    that require cross-unit naming
}

namespace carven::api:: /* canonical module path */ {
    // 7. export(cpp) outward adapters
}
```

Private nominal forward declarations and definitions stay in the same anonymous namespace. The same
placement applies to future private class forms. Structural visibility validation prevents a private
type from leaking into a wider declaration surface. Source module constants do not have a placement
here: under the current semantics they are compile-time facts with no runtime storage, address, or
linkage. Any backend-created helper materialization is compiler-owned implementation detail rather than
the source constant itself.

Raw companion content remains global and chooses its own helper linkage. An imported adapter's actual
C++ linkage is author-owned and visible only to the owning module implementation. The generated Carven
bridge follows the declaration's `private`/bare/`export` audience and existing Carven linkage rules.

### Artifact and downstream build contract

**Maturity:** Accepted for V1.

The compiler continues to emit source artifacts, not native libraries. `export(cpp)` adds the stable
public header; its implementation remains in the owning module `.cpp`. The downstream build:

- compiles the generated module `.cpp` together with a C++ consumer, or
- links its object files/static library into the consumer.

V1 does not emit or promise shared-library import/export macros, symbol-visibility annotations, a stable
ABI, or cross-compiler compatibility. Imported headers do not imply linked libraries. Xmake/project
configuration owns packages, objects, static archives, link flags, and search paths.

Two independently produced libraries that intentionally choose the same canonical module path and C++
surface own the resulting collision; Carven does not add an artifact-root hash to the public API.

### Responsibility and diagnostics boundary

**Maturity:** Accepted.

| Responsibility | Owner |
| --- | --- |
| Form grammar, supported declaration kind, visibility, access, effects, and carrier closure | Carven |
| C++ keyword/name representability known before generation | Carven |
| Canonical public header path, namespace, declaration, and adapter generation | Carven |
| Header contents, macros, C++ redeclarations, function bodies, templates, and overload presence | C++ author/toolchain |
| Adapter definition, inline/ODR correctness, vendor library, and link satisfaction | C++ author/build/toolchain |
| Exception containment, Unicode validity, lifetime, UB, and external semantic truth | Boundary author |

Carven diagnostics anchor malformed forms and representability failures in `.cv` source. Generated
declarations and raw regions retain source attribution so downstream C++ diagnostics can identify the
owning declaration or companion span. Carven does not parse C++ merely to replace a normal target
diagnostic.

## Whole-system simulation checkpoint

The 2026-08-31 simulation followed representative private, bare, `export`, companion-defined,
header-defined, linked-library, same-spelled cross-module, callable-view, and C++ consumer cases through
parse, semantic analysis, reference planning, SCC interfaces, target artifacts, C++ compilation, linking,
and runtime validation. The core owner-bridge design closes against the current backend. Its three
discovered blockers were resolved by `CPP-26` through `CPP-28` before implementation began.

## Decision record

| ID | Decision | Design | Rationale |
| --- | --- | --- | --- |
| `CPP-01` | Core forms use `head(form)`: `class(interface)`, `import(cpp)`, `export(cpp)`. | [Head-owned forms](#head-owned-forms) | The construct owns its mutually selected semantic form; it is not metadata. |
| `CPP-02` | `@` is not used for core forms. | [Head-owned forms](#head-owned-forms) | ABI, audience, declaration kind, and body authority are not orthogonal annotations. |
| `CPP-03` | `#[cpp]` is top-level module companion source; multiple blocks form one ordered logical source. | [C++ companion source](#c-companion-source) | Arbitrary C++ stays in one explicit lexical boundary. |
| `CPP-04` | Expression/statement `#[cpp]` and `Foreign` are removed by V1. | [C++ companion source](#c-companion-source) | C++ must not bypass the typed adapter contract from inside ordinary Carven evaluation. |
| `CPP-05` | Header dependencies use `import <...>` and `import "..."`; they import no Carven facts and link no library. | [Structured header dependencies](#structured-header-dependencies) | Header visibility and build/link ownership remain distinct. |
| `CPP-06` | `import(cpp)` declares a Carven function implemented by one strict C++ adapter. | [`import(cpp)` is a Carven declaration with a C++ implementation](#importcpp-is-a-carven-declaration-with-a-c-implementation) | Call sites remain ordinary Carven; C++ complexity stays at the provider boundary. |
| `CPP-07` | `import(cpp)` uses normal Carven `private`/bare/`export` visibility. | [`import(cpp)` is a Carven declaration with a C++ implementation](#importcpp-is-a-carven-declaration-with-a-c-implementation) | Implementation origin and Carven audience are independent axes. |
| `CPP-08` | The adapter is the same-spelled unique function found by qualified lookup `::name`; keywords and known generated-context collisions (`main`, `std`, `carven`) are rejected without broader underscore-policy linting. | [Adapter name and declaration](#adapter-name-and-declaration) | V1 avoids rename/namespace machinery, rejects functional impossibilities, permits unnamed-namespace lookup where C++ does, and does not over-police author-owned C++ usage. |
| `CPP-09` | Superseded by `CPP-24`: Carven does not synthesize the adapter's C++ declaration. | [Adapter name and declaration](#adapter-name-and-declaration) | The visible user declaration plus owning bridge covers normal C++ integration without inventing a prototype. |
| `CPP-10` | The adapter must be a unique, addressable, exact `noexcept` function, checked through an evaluated untargeted address deduction. | [Adapter name and declaration](#adapter-name-and-declaration) | Target-typed selection could hide overloads; an unevaluated-only check would not reject immediate functions. |
| `CPP-11` | V1 uses a closed scalar `CppCarrier` table. | [Scalar carrier closure](#scalar-carrier-closure) | A finite representability gate is complete and reviewable. |
| `CPP-12` | V1 parameters are Read/by-value and functions are concrete and infallible. | [Scalar carrier closure](#scalar-carrier-closure) | Ownership, mutation, generic, and failure protocols remain out of the minimal boundary. |
| `CPP-13` | `char` uses checked `char32_t`; invalid inbound values terminate. | [Scalar carrier closure](#scalar-carrier-closure) | C++ representation alone does not prove the Carven Unicode scalar invariant. |
| `CPP-14` | `export(cpp)` includes ordinary complete-compilation export and adds the C++ audience. | [`export(cpp)` audience and source closure](#exportcpp-audience-and-source-closure) | Audience grows monotonically and needs no multi-form syntax. |
| `CPP-15` | `export import(cpp)` is allowed; `export(cpp) import(cpp)` is rejected in V1. | [`export(cpp)` audience and source closure](#exportcpp-audience-and-source-closure) | Ordinary Carven reuse is useful; direct re-export is a deliberately deferred composition rather than a functional impossibility. |
| `CPP-16` | V1 `export(cpp)` accepts only scalar top-level functions. | [`export(cpp)` audience and source closure](#exportcpp-audience-and-source-closure) | Import and export share one minimal representability closure. |
| `CPP-17` | Public headers use `carven/api/<module>.hpp` and `carven::api::<module>`. | [C++ consumer surface](#c-consumer-surface) | Canonical module identity creates a stable façade without exposing private hashes. |
| `CPP-18` | Outward adapters are generated `noexcept` functions with directional `char` validation. | [C++ consumer surface](#c-consumer-surface) | C++ consumers receive one exact source contract without exception translation. |
| `CPP-19` | Private generated functions and nominal types, plus compiler-owned runtime helpers/materializations, live in a nested anonymous namespace; source constants remain storage-free facts. | [Generated placement and linkage](#generated-placement-and-linkage) | Runtime entities receive genuine internal linkage without inventing storage for compile-time constants or wrapping opaque C++. |
| `CPP-20` | Raw companion scope and provider linkage remain author-owned; generated owning bridges follow Carven visibility and linkage. | [Generated placement and linkage](#generated-placement-and-linkage) | Carven must not change arbitrary C++ namespace/linkage semantics, while cross-module callers must stay behind the Carven interface. |
| `CPP-21` | V1 promises generated source plus direct object/static linkage, not shared-library or stable ABI support. | [Artifact and downstream build contract](#artifact-and-downstream-build-contract) | The current compiler/build ownership is preserved and the first slice remains finite. |
| `CPP-22` | Superseded by `CPP-25`: the prior compilation-global provider-name table is withdrawn. | [Adapter identity and owning bridge](#adapter-identity-and-owning-bridge) | Without a generated external declaration, equal raw spellings in separate owner translation units neither prove nor require one C++ entity. |
| `CPP-23` | Superseded by `CPP-24`: no compiler-generated adapter declaration is ordered ahead of user headers. | [Adapter name and declaration](#adapter-name-and-declaration) | The header or companion is now the declaration authority. |
| `CPP-24` | User headers/companion first make the adapter visible; Carven validates an evaluated exact address and models one logical owning bridge without generating a C++ adapter declaration. | [Adapter name and declaration](#adapter-name-and-declaration) | Ordinary C++ header/source visibility remains valid without inventing or potentially conflicting with a user-owned prototype. |
| `CPP-25` | Raw adapter bindings are module-local. Same-spelled imports in different modules are independent Carven capabilities; an unqualified consumer import collision is a usage error and does not invalidate either producer. | [Adapter identity and owning bridge](#adapter-identity-and-owning-bridge) | Canonical Carven module identity and owner-local C++ lookup separate the Carven bindings; actual C++ entity identity remains author/toolchain-owned. |
| `CPP-26` | Structured header imports preserve their header-name spelling, lower to the matching C++ `#include` form, and follow ordinary downstream C++ preprocessor search rules. | [Structured header dependencies](#structured-header-dependencies) | Header lookup belongs to the C++ build boundary; inventing `.cv`-relative resolution would require a new physical-source model and would not work uniformly for virtual inputs. |
| `CPP-27` | Every `import(cpp)` materializes exactly one owner bridge; private changes its linkage, not the function identity used by calls and callable views. | [Adapter identity and owning bridge](#adapter-identity-and-owning-bridge) | The bridge owns validation and carrier conversion, prevents macro-dependent rebinding, and gives one declaration one observable implementation identity. |
| `CPP-28` | Function identity and signature are separate from an explicit `CarvenBody | CppImport` implementation origin; a C++ import never receives a synthetic Carven body. | [Semantic implementation origin](#semantic-implementation-origin) | Honest data modeling keeps body analyses, interop validation, callable identity, and lowering responsibilities distinct instead of encoding one domain as a false instance of another. |

## Deferred work

### DEFER-CPP-01 — C ABI forms

- **Reason deferred:** `import(c)`/`export(c)` need calling convention, stable symbol, boolean, layout,
  and cross-toolchain rules distinct from the same-toolchain C++ contract.
- **Depends on:** Implemented C++ scalar V1 and a concrete C ABI consumer/provider.
- **Reactivation condition:** A supported binary or non-C++ integration requires a C ABI.

### DEFER-CPP-02 — Transparent structs, numeric enums, and arrays

- **Reason deferred:** Aggregate carrier construction, layout independence, recursive validation, and
  generated pack/unpack glue require a separately closed value slice.
- **Depends on:** Implemented scalar V1.
- **Reactivation condition:** A concrete adapter needs a value richer than separate scalar parameters.

### DEFER-CPP-03 — Text, pointers, handles, ownership, and lifetime

- **Reason deferred:** `str`, views, opaque objects, nullable values, allocation, and destruction cannot
  be represented honestly by an unqualified raw pointer.
- **Depends on:** Stable pointer/handle and ownership/lifetime designs.
- **Reactivation condition:** A real interop API supplies complete owner, borrow, nullability, and release
  behavior.

### DEFER-CPP-04 — Write and Take access

- **Reason deferred:** Write needs mutation/aliasing rules and Take needs an enforceable post-call
  availability/ownership transition; C++ references alone do not prove either contract.
- **Depends on:** Implemented scalar V1 and the relevant carrier ownership design.
- **Reactivation condition:** A concrete adapter requires mutation or ownership transfer.

### DEFER-CPP-05 — Failure and exception mapping

- **Reason deferred:** The private Carven Outcome lowering is not a public carrier and exceptions are not
  an implicit translation protocol.
- **Depends on:** Implemented scalar V1 and a chosen public failure representation.
- **Reactivation condition:** A consumer/provider needs recoverable typed failure across the boundary.

### DEFER-CPP-06 — Generics and C++ templates

- **Reason deferred:** Boundary symbols must be concrete; open Carven generics and C++ deduction cannot
  participate in each other's solvers.
- **Depends on:** Implemented scalar V1 and the generics instance contract.
- **Reactivation condition:** A concrete closed generic instance or template adapter is required.

### DEFER-CPP-07 — Callbacks, async, and threading

- **Reason deferred:** Callback context ownership, escape, deregistration, thread entry, cancellation,
  and executor behavior require independent contracts.
- **Depends on:** Ownership, memory-model, threading, and async decisions relevant to the use case.
- **Reactivation condition:** A concrete callback or asynchronous integration supplies an end-to-end
  lifetime and thread contract.

### DEFER-CPP-08 — Full C++ declaration import

- **Reason deferred:** Parsing arbitrary headers, overloads, templates, classes, ADL, special members,
  and lifetimes is a different product from a strict adapter.
- **Depends on:** A dedicated importer architecture and strong motivating use cases.
- **Reactivation condition:** Repeated adapters demonstrate that the finite bridge cannot meet supported
  C++ integration needs.

### DEFER-CPP-09 — Shared libraries, stable ABI, and façade customization

- **Reason deferred:** Visibility attributes, import/export macros, ABI versioning, symbol evolution,
  renames, and alternate signatures exceed the source-level static/object V1.
- **Depends on:** Implemented scalar V1 and a concrete distribution contract.
- **Reactivation condition:** A supported shared/binary distribution or public façade cannot use the
  direct generated surface.

## Implementation

The design is accepted and implementation has not started. The first vertical delivery must update
grammar/AST, introduce an explicit bodyless C++-import implementation variant throughout semantic and
body-consuming passes, add representability validation, remove non-top-level `#[cpp]` and `Foreign`, record
adapter/consumer facts in SemanticProgram, plan public artifacts and private placement, lower both
adapter directions, update the Xmake rule, and hand the implemented behavior to permanent documentation.

### Expected implementation path

1. **Make the function model honest without changing language behavior.** Introduce the explicit
   `CarvenBody | CppImport` implementation origin, migrate existing functions to `CarvenBody`, move shared
   signature/parameter facts out of body-only storage where necessary, and make every body-consuming pass
   visit only real Carven bodies.
2. **Parse and validate source-level interop facts.** Add head-owned forms, bodyless `import(cpp)`
   declarations, structured header names, and the ordered module companion model. Semantic analysis closes
   visibility, effects, scalar carriers, names, and implementation origin before target planning.
3. **Plan ownership with existing module/SCC machinery.** Record one owner binding and bridge per imported
   function. Private bridges use the module's anonymous namespace; bare and `export` bridges enter the
   existing published surface and cross-module reference plan. User headers and companion bytes remain
   owner-local.
4. **Complete the `import(cpp)` vertical path.** Emit compiler/interface headers, structured `#include`
   directives, companion bytes, the evaluated exact-address check, and the owner bridge in that order.
   Centralize carrier conversion and inbound `char` validation in the bridge, then exercise private,
   cross-module, function-value, header, companion, and linked-provider cases.
5. **Add the C++ consumer path.** Validate the compilation-wide public namespace tree, emit a standalone
   `carven/api/<module>.hpp` target artifact, and generate outward `export(cpp)` adapters in the owner
   implementation. Extend the downstream rule only to write, expose, compile, and link these declared
   artifacts; package publication remains build-owned.
6. **Retire the superseded escape path and close the slice.** Remove expression/statement `#[cpp]` and
   `Foreign`, make the Unicode boundary helper wording interop-neutral, update permanent documentation,
   and finish with the complete language/build/link/runtime evidence.

After the shared semantic facts in steps 1–2 stabilize, owner-bridge lowering and public-header artifact
plumbing can proceed independently. They converge at target planning and should be integrated through the
`import(cpp)` path before the outward `export(cpp)` surface is treated as complete.

The compiler must continue to make every Carven validity decision before target lowering. C++ checks
only header/body/redeclaration/definition/link truth that Carven cannot know without parsing C++.

## Validation

The V1 evidence must include:

- grammar acceptance and rejection for every form and visibility composition;
- explicit `CarvenBody | CppImport` semantic implementation variants, with no synthetic body facts or
  accidental traversal by body-only analyses;
- removal diagnostics for expression/statement `#[cpp]` and `Foreign` containment paths;
- every scalar parameter/result carrier in both directions under C++20 and C++23;
- `char` success and invalid-inbound termination in both directions;
- rejection of `str`, Write, Take, failure, generics, aggregates, methods, and invalid C++ names;
- header-provided, inline-header, linked-library, and companion-defined imported adapters;
- absence of a generated C++ adapter declaration; header/companion visibility before validation and the
  logical owning boundary;
- materialized private/bare/export owning bridges, callable-view adoption through the same bridge, and
  function-like macro cases that cannot change the validated binding;
- accepted ordinary external, `static`, unnamed-namespace, C-linkage, `inline`, and `constexpr`
  providers, plus rejected `consteval`, deleted, and non-addressable providers;
- exact type, missing `noexcept`, overload/template-set, C++ default-argument, downstream definition,
  and ODR cases;
- multiple companion blocks, exact structured-header spelling under ordinary downstream C++ search
  rules, generated-translation-unit-relative quote behavior, and raw include behavior;
- stable, self-contained, repeat-includable `carven/api/<module>.hpp` declarations used by handwritten
  C++ consumers;
- private function/struct/class/enum placement in the generated anonymous namespace and absence of
  runtime storage for source module constants;
- independent same-spelled raw adapter bindings in different modules, owner-only header/companion
  visibility, and ordinary unqualified Carven import conflicts at the consumer;
- rejection of `main`/`std`/`carven` adapter names and public function/namespace prefix collisions;
- direct same-target/object/static linking and explicit absence of shared-library promises;
- tests that assert only stable source behavior, diagnostics, public paths/names/signatures, and runtime
  results rather than full generated C++ text.

## References

- [Carven philosophy](../docs/philosophy.md)
- [Carven semantics](../docs/semantics.md)
- [Carven grammar](../docs/grammar.md)
- [Carven compiler model](../docs/compiler.md)
- [Carven backend](../docs/backend.md)
- [Compatibility boundary](../docs/compatibility.md)
- [Classes and class forms](classes.md)
- [Generics and static constraints](generics.md)
- [Memory model](memory-model.md)
- [Proposal roadmap](roadmap.md)
