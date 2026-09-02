# Compatibility

This document defines the supported compiler host, generated-C++ consumer
modes, native numeric-model requirements, and stability boundary of Carven
output.
It does not define Carven syntax, language behavior, command invocation, or
filesystem policy.

Carven is experimental. Its compatibility surface is limited to the contracts
stated below.

## Compiler host

Building the compiler requires Xmake and an LLVM/Clang toolchain capable of
compiling C++26. The compiler implementation is built as C++26 with C++
exceptions disabled. The validated host baseline is LLVM/Clang and libc++
23.1.0. Another host toolchain must implement the C++26 language and
standard-library features used by `src/` and `tests/internal/`; Carven does not
carry compiler-host feature branches for older implementations.

## Generated-C++ consumers

Generated C++ and crafts target C++20. C++20 and C++23 are supported
generated-C++ consumer modes. The downstream build selects its consumer mode;
Carven generation is mode-independent. Repository evidence for each mode is
defined by [testing.md](testing.md#consumer-coverage).

Host and consumer standards are separate axes. C++26 library or language
features used to implement the Carven compiler must not leak into generated
source or crafts unless the generated-C++ baseline is changed by a separate
compatibility decision.

## Compiler and toolchain boundary

Carven emits C++20 artifacts and does not invoke a downstream C++ compiler.
Given downstream inputs that satisfy the C++ responsibilities below, artifacts
for a semantically valid Carven compilation must compile in every supported
consumer mode; failure to do so is a Carven compatibility defect.

The downstream toolchain performs C++ compilation and linking and determines
platform ABI, object layout, and machine code. C++ header/source-fragment
contents, provider declaration conformance, definitions, and link satisfaction
are checked by that toolchain under the responsibility defined by
[semantics.md](semantics.md#c-interoperation).

Toolchain diagnostics remain toolchain diagnostics. Generated source
attribution may identify a `.cv` location but does not change diagnostic
ownership.

## Native numeric model

Carven `isize` and `usize` use the compiler host's `std::ptrdiff_t` and
`std::size_t` widths during semantic analysis and the downstream target's
matching C++ types at runtime. A supported compilation therefore requires the
compiler host and downstream target to share that data model. Cross-model
generation is unsupported.

Carven `f32` and `f64` use the compiler host's C++ `float` and `double` during
semantic analysis and the downstream target's matching types at runtime. A
supported host and downstream target must therefore provide IEEE 754 binary32
as `float` and binary64 as `double`, including their standard sizes, precision,
and exponent ranges. The installed runtime headers statically enforce the
downstream requirement; generation across different floating-point models is
unsupported.

## Stable integration surface

The integration surface is divided among these owning contracts:

| Surface | Owning contract |
| --- | --- |
| Generated-C++ consumption in C++20 and C++23 | This document |
| Observable Carven behavior, diagnostics, and C++ boundary validity | [semantics.md](semantics.md) |
| Command options, status, and streams | [cli.md](cli.md) |
| Filesystem output behavior | [cli.md](cli.md) |
| Installed headers required by generated artifacts | This document |

## Generated artifacts

One closed compilation emits these logical paths:

```text
carven/generated/<component-anchor>.hpp  # one per published-surface SCC
carven/api/<canonical-module-path>.hpp   # one per module with export(cpp)
<canonical-module-path>.cpp
carven-test-main.cpp                     # only for default test mode
```

Canonical module path components are `/`-separated in logical artifact paths.
Implementations mirror the canonical module path, while interface component
headers live in the target-private `carven/generated/` include hierarchy.
Filesystem sinks may materialize these paths below any selected output root. A
module with no published semantic surface has no component of its own. A
component is anchored to its lexicographically first canonical member path.
SCC membership and therefore component count or anchor can change with
published complete-definition dependencies. Declaration-only dependencies can
instead be represented by deterministic C++ forward declarations. Output roots
do not carry compiler ownership metadata; stale-path retirement is a
build-system or caller responsibility.

### C++ interoperation artifacts

C++ header imports produce matching `#include` directives in source order in
the owning module implementation. Each C++ source fragment is preserved as an
independent attributed payload and appears at global scope, in source order,
after implementation includes and before compiler-generated namespaces.

`carven/api/<canonical-module-path>.hpp` is the explicit C++ consumer surface.
It is self-contained and safe to include more than once. It declares the
module's `export(cpp)` functions under `carven::api` followed by the canonical
module components as nested namespaces, using the scalar spellings defined by
[semantics.md](semantics.md#c-interoperation). The corresponding façades are
defined by that module's implementation artifact. The public path, namespace,
function names, declarations, and `noexcept` contract are stable; include
selection and generated body shape are private.

## Installed support headers

Generated artifacts consume exactly these installed headers:

```text
carven/runtime/runtime.hpp
carven/runtime/callable.hpp
carven/runtime/outcome.hpp
carven/std/testing/testing.hpp
```

The first three form the generated runtime support surface. Artifacts with test
support use the testing header.

## Private generated implementation

The following are not compatibility promises:

- complete generated C++ text, whitespace, or declaration layout;
- private generated namespaces, identifiers, temporaries, labels, or helper
  selection outside the documented `carven::api` surface;
- `LinkageDomainID` and `ModuleNamespaceID` bytes and hash-input encoding;
- the grouping of declarations inside a generated unit beyond the documented
  logical artifact paths;
- private runtime helper types, methods, representation, or platform ABI;
- `SemanticProgram`, `TargetProgram`, and `TargetUnit` private layouts,
  compiler representation IDs, internal module partitions, or stage-local
  construction state;
- diagnostic prose, notes, formatting, colors, or incidental ordering.

Support headers are compiler dependencies for generated artifacts and are not
part of the public C++ API. The `export(cpp)` header and façade contract is the
explicit consumer surface; accessibility of another generated or runtime name
does not add it to that surface.
