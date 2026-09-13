# Toolchain and artifacts

This document specifies native build requirements, generated artifact interfaces,
and build integration for this checkout.

## Compiler and target

Compiler implementation uses C++26 with exceptions and RTTI disabled. The
validated host is LLVM/Clang and libc++ 23.1.0. Generated programs and installed
crafts have a C++20 minimum baseline. The consumer project selects its C++
standard. Generated code and runtime support may use newer available facilities
through capability-dependent implementations that preserve the same Carven
semantic contract and functionality. Host-only features stay within the compiler
implementation.

The native consumer build selects exception support for its sources and
providers according to their C++ requirements. A `#[cpp]` fragment containing
native `throw` or `try`/`catch` requires exception support in its generated
translation unit.

Carven is a source-generation step. The build system supplies source batches,
C++ providers, libraries, include paths, and native compiler options, then
compiles and links the artifacts. C++ validates provider declarations and
protocols, object definitions, and link requirements. It also checks overloads,
templates, and conversions for explicitly delegated operations.

Semantic acceptance is one stage of compilation. Native compilation and linking
must also succeed. Invalid target C++ for a supported Carven operation, after
satisfying its provider requirements, is a compiler defect. The construction
limitation below describes a current gap in that support.
C++ diagnostics remain native toolchain diagnostics; source mapping identifies
the corresponding Carven location.

### Construction limitation

An aggregate initializer may evaluate a native component before a later component
that can produce a Carven failure. If lowering saves the earlier component in
separate storage, final aggregate construction must copy or move it from that
storage. An immovable component cannot satisfy this generated construction,
even when C++ could construct it directly in the final aggregate.

Carven semantic analysis does not establish native constructor availability;
this limitation can therefore appear as a C++ compilation error after successful
Carven analysis. It does not prohibit all immovable results: direct construction
from a prvalue remains possible where no intermediate transfer is required.

## Numeric model

Carven `isize` and `usize` use the host's pointer-sized integer widths during
analysis and matching target types at runtime. Host and target data models must
match. `f32` and `f64` require IEEE 754 binary32 and binary64; runtime headers
check the target properties. Native options must preserve IEEE equality and
Carven evaluation order.

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
`carven::api`, followed by nested namespaces for the encoded module path
components. Its implementation contains the corresponding façades. The header
is self-contained.

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

An Xmake project declares the Carven repository and package dependency, then
attaches `@carven/carven` to its target:

```lua
add_repositories("carven-xmake-repo https://github.com/ryblust/carven-xmake-repo.git")
add_requires("carven")

target("app")
    set_kind("binary")
    add_rules("@carven/carven")
    add_files("src/**.cv")
```

The rule obtains the compiler and matching Crafts from the required package.
It uses the installed `crafts/carven/` and project-root `crafts/` as source roots,
and both `crafts/` directories as native include roots. A missing project
`crafts/` contributes no files. Xmake file matching collects `.cv` and optional
`.cpp` files. Test sources live under the project's `tests/` directory and are
added explicitly by test targets. Inline tests follow the target's test emission
mode.

Application targets list their own sources. Toolchain paths and module mappings
require no user configuration. Native provider dependencies and optional test
modes use ordinary target configuration.

The rule passes the complete `.cv` batch to Carven before native dependency
scanning, supplies a target-private output root and linkage domain, and registers
generated implementations as C++ sources. Installed source paths preserve their
`crafts/carven/` hierarchy. The compiler derives module identities from the supplied
filenames and diagnoses duplicates. The [module rules](semantics.md#compilations-crafts-and-modules)
define import resolution and the reserved `std::` prefix.

Generation runs in a staging directory before updating live artifacts. A failed
compiler invocation leaves live output intact; promotion itself can fail partway
through. The [rule repository](https://github.com/ryblust/carven-xmake-repo) owns
batch scheduling, incremental promotion, and recovery.

The installed layout places `crafts/` beside `bin/`. The official `carven` craft
contains runtime support and the standard library. Capability modules keep their
API documentation alongside their sources, as in
[UTF](../crafts/carven/std/utf/README.md). Native headers and sources remain with
their owning capability. Runtime support is independent of generated
standard-library code.

## Support headers

Generated files include the self-contained support leaves they use:

```text
carven/runtime/passing.hpp
carven/runtime/numeric.hpp
carven/runtime/array.hpp
carven/runtime/slice.hpp
carven/runtime/text.hpp
carven/runtime/utf.hpp
carven/runtime/string.hpp
carven/runtime/format.hpp
carven/runtime/print.hpp
carven/runtime/entry.hpp
carven/runtime/outcome.hpp
carven/runtime/callable.hpp
carven/runtime/unreachable.hpp
carven/runtime/testing.hpp
```

`carven/runtime/runtime.hpp` aggregates runtime leaves for direct consumers.
The compiler and its generated support are developed together. Private target
names, helper selections, and representation layouts are implementation details.
Use support headers matching the compiler that generated the artifacts.

`passing.hpp` supplies native parameter and transfer support; `entry.hpp`
supplies process-argument ingress. `testing.hpp` supplies inline-test execution
contexts, failure records, and reporting in `carven::runtime`.

`utf.hpp` supplies UTF validation, scalar encoding and decoding, and native
representation conversions. `text.hpp` supplies text views; `string.hpp` supplies owning
String. Interpolation uses `format.hpp` and requires C++20 `<format>` support
in the consumer's standard library. The consumer compiler checks format strings
and the availability of formatters for native types. `print.hpp` supplies stdout
and stderr printing, selecting the C++20 implementation or available C++23 library
print support without changing the consumer's selected standard.

For direct C++ calls, `String::from_str` and `append` require valid UTF-8, and
`push` requires a Unicode scalar. `String::from_utf8` validates incoming byte
storage and terminates on invalid UTF-8. Runtime String operations support C++20
constant evaluation; this does not make String a Carven constant-expression type.
