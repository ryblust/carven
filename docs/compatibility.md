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
exceptions disabled.

## Generated-C++ consumers

Generated C++ and crafts have one C++20 implementation without standard-mode
branches. C++20 and C++23 are supported generated-C++ consumer modes. Routine
verification executes the complete language corpus in C++20, compiles that
corpus in C++23, and executes the focused C++ boundary corpus in both modes.
The downstream build selects its consumer mode; Carven generation is unchanged.

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
| Observable Carven behavior, diagnostics, and the `#[cpp]` boundary | [semantics.md](semantics.md) |
| Command options, status, and streams | [cli.md](cli.md) |
| Filesystem output behavior | [cli.md](cli.md) |
| Installed headers required by generated artifacts | This document |

## Generated artifacts

One closed compilation emits these logical paths:

```text
carven/generated/<component-anchor>.hpp  # one per published-surface SCC
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

## Installed support headers

Generated artifacts consume exactly these installed headers:

```text
carven/runtime/runtime.hpp
carven/runtime/callable.hpp
carven/runtime/outcome.hpp
carven/std/testing/testing.hpp
```

The first three form the generated runtime support surface. The testing header
is required only by artifacts that include test support.

## Private generated implementation

The following are not compatibility promises:

- complete generated C++ text, whitespace, or declaration layout;
- generated namespaces, private identifiers, temporaries, labels, or helper
  selection;
- `LinkageDomainID` and `ModuleNamespaceID` bytes and hash-input encoding;
- the grouping of declarations inside a generated unit beyond the documented
  logical artifact paths;
- private runtime helper types, methods, representation, or ABI;
- compiler representation IDs, internal module partitions, or stage-local
  construction state;
- diagnostic prose, notes, formatting, colors, or incidental ordering.

Support headers are compiler dependencies for generated artifacts, not a
general handwritten-C++ API. An explicit C++ consumer surface requires its own
documented contract; incidental accessibility of a generated or runtime name
does not create one.
