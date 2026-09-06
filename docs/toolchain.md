# Toolchain and artifacts

This document specifies the current native build inputs and generated artifact
layout.

## Compiler and target

Compiler implementation uses C++26 with exceptions and RTTI disabled. The
validated host is LLVM/Clang and libc++ 23.1.0. Generated programs and installed
crafts use C++20. Host-only features stay within the compiler implementation.

The native consumer build selects exception support for its sources and
providers according to their C++ requirements. A `#[cpp]` fragment containing
native `throw` or `try`/`catch` requires exception support in its generated
translation unit. Generated exception specifications follow the
[native exception boundary](semantics.md#native-exception-boundary).

Carven is a source-generation step. The build system supplies source batches,
C++ providers, libraries, include paths, and native compiler options, then
compiles and links the artifacts. C++ validates provider declarations and
protocols, object definitions, and link requirements. It also checks overloads,
templates, and conversions for explicitly delegated operations.

For Carven-owned operations and declared boundary requirements, successful
analysis must produce valid target C++ subject to those provider requirements.
C++ diagnostics remain native toolchain diagnostics; source mapping identifies
the corresponding Carven location.

## Numeric model

Carven `isize` and `usize` use the host's pointer-sized integer widths during
analysis and matching target types at runtime. Host and target data models must
match. `f32` and `f64` require IEEE 754 binary32 and binary64; runtime headers
check the target properties. Native floating operations use the selected C++
compiler and floating environment. Carven does not set the rounding mode or
promise bit-identical floating results across toolchains. Native options that
discard the language's IEEE equality or evaluation-order requirements are
outside this contract.

## Read parameter realization

Ordinary Read parameters and Read array-range bindings use a native type
policy. A type with trivial C++ copy construction and destruction is passed
as a const value, regardless of size. Other types use a const reference to
avoid introducing user-defined copying or destruction. The C++ compiler and
target ABI determine how value parameters are physically passed.
The scalar C++ interoperability boundary has its own by-value rules in
[semantics.md](semantics.md#c-interoperation).

A by-value Read argument saves its value when that argument is evaluated. A
by-reference Read argument retains the selected storage, so writes through
another alias can affect subsequent reads. Read access alone is therefore not
a uniform snapshot mechanism. An explicit owning copy establishes a separate
value before the call; non-owning contents in that copy retain their referents.
Both representations obey the same source access markers and Take-conflict
checks.

## Artifact paths

```text
carven/generated/<component-anchor>.hpp
carven/api/<canonical-module-path>.hpp
<canonical-module-path>.cpp
carven/generated/carven-test-runner.hpp
carven/generated/carven-test-main.cpp
```

An interface component contains declarations connected by complete-definition
requirements. Its anchor is its first canonical module path. Modules without a
published semantic surface have no component of their own. Implementations use
canonical module paths.

The `carven/api` header contains explicit `export(cpp)` declarations in
`carven::api` followed by the module namespace components. Its implementation
contains the corresponding façades. The header is self-contained.

C++ header imports become ordered includes in the owning implementation. An
external type or result query needed by an interface brings its context module's
complete header import list into that interface, including imports without
`using`. Module-scoped lookup additionally brings the complete using environment.
Each artifact materializes a module environment once, preserving that module's
import order, delimiter forms, and explicit repetitions. Different environments
follow stable module order; this does not reproduce arbitrary macro configuration
orders across modules. Raw source fragments remain implementation-only and follow
includes at global scope before generated namespaces.

Test generation emits the runner header. Default test generation also emits a
main source; external test generation supplies the runner to a caller-provided
entry. Both use the same direct test functions and reporting protocol. The generated
runner invokes modules in canonical path order and tests within each module in
source order.

Logical paths are relative to the output destination. The build system owns
stale artifact removal, installation, and native dependency scheduling.

## Build integration

The standalone CLI writes artifacts directly and does not compile C++.
Consumers provide the output root and installed support root as include search
paths, compile the generated implementations, and link their C++ providers.
Header imports do not add include directories or link inputs.

The packaged `@carven/carven` Xmake rule owns batch scheduling and output
promotion. It invokes Carven on a target's complete explicit `.cv` set before
native dependency scanning, then registers the generated `.cpp` files as
ordinary C++ sources. Its default C++ language is C++20 when the target has not
selected one. Its default linkage domain combines the normalized absolute
project directory and `target:fullname()`.

Generation uses a disposable staging directory and a target-private live root.
A failed compiler invocation leaves the live output intact. For an unchanged
artifact path set, promotion preserves content-identical files and their
modification times; a changed path set replaces the live tree. Promotion failure
may leave a partial update and invalidates the dependency cache for repair on
the next build. This behavior belongs to the rule, not the standalone CLI.

Package options and consumer setup are maintained in the
[rule repository](https://github.com/ryblust/carven-xmake-repo).

## Support headers

Generated files include the self-contained support leaves they use:

```text
carven/runtime/passing.hpp
carven/runtime/numeric.hpp
carven/runtime/array.hpp
carven/runtime/text.hpp
carven/runtime/entry.hpp
carven/runtime/outcome.hpp
carven/runtime/callable.hpp
carven/runtime/unreachable.hpp
carven/std/testing/testing.hpp
```

`carven/runtime/runtime.hpp` aggregates runtime leaves for direct consumers.
The compiler and its generated support are developed together. Private target
names, helper selections, and representation layouts are implementation details.

`passing.hpp` supplies native parameter and transfer support; `entry.hpp`
supplies process-argument ingress.
