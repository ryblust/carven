# Toolchain and artifacts

This document specifies native build requirements, generated artifact interfaces,
and build integration for this checkout.

## Compiler and target

Compiler implementation uses C++26 with exceptions and RTTI disabled. It uses
LLVM/Clang and libc++ across platforms, supplied by LLVM-MinGW on Windows.
LLVM 23 is the validated toolchain version. Generated programs and installed
crafts have a C++20 minimum baseline. The consumer project selects its C++
standard. Generated code and runtime support select newer facilities through
feature detection while preserving the operation's behavior.
Compiler-host features do not raise the generated-program or installed-craft
baseline.

The native consumer build selects exception support for its sources and
providers according to their C++ requirements. A `#[cpp]` fragment containing
native `throw` or `try`/`catch` requires exception support in its generated
translation unit.

The compiler core provides semantic analysis, execution, C++ generation, and
source and artifact facts. The CLI supplies default Crafts collection and a
direct-run driver that compiles, links, and executes a temporary native program.
An external build system owns project targets, dependency acquisition, source
selection, native configuration, caching, and scheduling. It invokes Carven as a
source-generation step, then compiles and links the artifacts and C++ providers.
C++ validates provider declarations and
protocols, object definitions, and link requirements. It also checks overloads,
templates, and conversions for explicitly delegated operations.

Successful builds require Carven analysis, native compilation, and linking.
Native compilation checks delegated operations and reports C++ diagnostics with
source mapping to the corresponding Carven location. The construction limitation
below can cause native compilation to fail after successful Carven analysis.
For a supported operation with satisfied provider requirements, invalid generated
C++ is a compiler defect.

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
carven/providers/<canonical-module-path>.hpp
<canonical-module-path>.cpp
carven/generated/carven-test-runner.hpp
carven/generated/carven-test-main.cpp
```

An interface component contains declarations connected by complete-definition
requirements. Its anchor is its first canonical module path. Modules without a
published semantic surface have no component of their own. Implementations use
canonical module paths. An implementation also realizes the static instances its
module calls, as `inline` functions in the provider's module namespace. Imported
staged function bodies contribute to the calling module's generated implementation.
Calling a function with new static values changes the caller's implementation;
the provider's generated artifacts remain independent of its callers.

The `carven/api` header contains explicit `export(cpp)` declarations in
`carven::api`, followed by nested namespaces for the encoded module path
components. Its implementation contains the corresponding façades. The header
is self-contained.

Modules containing concrete `import(cpp)` declarations produce a
`carven/providers` header with signature type aliases and complete type
dependencies. Native authors include it when naming Carven representations;
function declarations remain in authored C++ headers or source fragments. See
[provider type support](../language/interop.md#provider-type-support) for aliases,
type identity, and dependency requirements.

C++ header imports become ordered includes in the owning implementation. An
external type or result query needed by an interface brings its context module's
complete header import list into that interface, including imports without
`using`. Module-scoped lookup additionally brings the complete using environment.
Each artifact materializes a module environment once, preserving that module's
import order, delimiter forms, and explicit repetitions. Different environments
follow canonical module-path order; this does not reproduce arbitrary macro
configuration orders across modules. Raw source fragments remain
implementation-only and follow includes at global scope before generated namespaces.

Generated implementations include their associated interface component first,
when one exists. Generated test entries include their runner header first. Native
header environments follow, then runtime headers, other generated interfaces,
and directly required standard-library headers. Compiler-generated dependencies
are merged and sorted within their groups; explicit native occurrences remain
independent and retain their repetitions. Generated headers begin with pragma-once.

Test generation emits the runner header. `--tests=main` also emits a
main source; `--tests=runner` supplies the runner to a caller-provided
entry. Both use the same direct test functions and reporting protocol. The generated
runner invokes modules in canonical path order and tests within each module in
source order.

Logical paths are relative to the output destination. The build system owns
stale artifact removal, installation, and native dependency scheduling.
`compile --artifact-manifest=<file>` publishes the workspace root,
actual input identities, and artifact paths, roles, source-mapping policies, and
implementation source ownership
as JSON projected from the existing generated artifact set. Build integration
uses this inventory instead of recovering artifact roles from generated source
bytes or recursively discovering output files. Native compiler dependency files
remain authoritative for C++ header dependencies.

## Build integration

The `compile` command collects fixed Crafts roots and writes generated artifacts;
it does not compile or copy collected `.cpp` sources. A manual native build
selects the generated implementations and C++ providers needed by its target,
including any required native initialization. A program using only header-based
runtime operations needs its own generated implementation and runtime headers.
The direct-run entry compiles all
generated implementations and collected native sources, then executes the
program in a temporary directory.
Consumers provide the output root and installed support root as include search
paths.
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

Application targets list their own sources. Module names use project-relative
paths with the `.cv` suffix removed. Directory separators define module
components; each directory name and file stem must match `[A-Za-z_][A-Za-z0-9_]*`.
Toolchain paths require no user configuration. Native provider
dependencies and optional test modes use ordinary target configuration.

The rule passes the complete `.cv` batch to Carven before native dependency
scanning, supplies a target-private output root and linkage domain, and registers
generated implementations as C++ sources. Installed source paths preserve their
`crafts/carven/` hierarchy. Xmake registers generated implementations before
native dependency scanning using the stable module-path-to-`.cpp` contract.
The manifest validates that registration and supplies the actual files to
promote and track. Implementation source ownership associates native per-file
settings with the originating `.cv` input.
The compiler deduplicates canonical file paths and rejects duplicate module
identities. The rule uses `--explicit-inputs`, so the CLI adds no other source roots. Imports
resolve against the supplied source batch and provided standard modules.
`std::` selects the canonical `crafts.carven.std` prefix.

The installed standard-library implementations and native support sources belong
to a shared object target owned by the package rule. Consumers retain their own
application linkage domain and compile the inline static instances they need.
The rule shares these objects among targets with matching compiler, Crafts, and
native compilation settings and prerequisite targets. Each consumer performs
full Carven analysis.
Project-local Crafts remain ordinary target inputs.

Reconfigure after adding or deleting installed Crafts files. The rule publishes
provider support headers before native compilation and supplies the standard
TCP provider's Winsock dependency on Windows. Manual builds that include this
provider must link `ws2_32`.

Generation runs in a staging directory before updating live artifacts. A failed
compiler invocation leaves live output intact; promotion itself can fail partway
through. Xmake dependency records track the source files, compiler, rule files,
invocation arguments, and generated outputs. Promotion preserves timestamps of
unchanged content. Xmake's C++ rules own native header dependencies, object
compilation, and linking.

The installed layout places `crafts/` beside `bin/`. The official `carven` craft
contains runtime support and the standard library. Native headers and sources
remain with their owning capability. Runtime support
is independent of generated standard-library code.

## Support headers

Include the self-contained support headers for the contracts used by generated
files and native consumers. Runtime support is grouped by domain:

```text
carven/runtime/call/callable.hpp
carven/runtime/call/passing.hpp
carven/runtime/collection/array.hpp
carven/runtime/collection/range.hpp
carven/runtime/collection/slice.hpp
carven/runtime/collection/vector.hpp
carven/runtime/diagnostics/observe.hpp
carven/runtime/diagnostics/report.hpp
carven/runtime/diagnostics/trap.hpp
carven/runtime/display/display.hpp
carven/runtime/execution/child.hpp
carven/runtime/execution/clock.hpp
carven/runtime/execution/completion.hpp
carven/runtime/execution/composition.hpp
carven/runtime/execution/entry.hpp
carven/runtime/execution/execution.hpp
carven/runtime/execution/time.hpp
carven/runtime/execution/timeout.hpp
carven/runtime/execution/unreachable.hpp
carven/runtime/format/format.hpp
carven/runtime/format/writer.hpp
carven/runtime/io/cursor.hpp
carven/runtime/io/print.hpp
carven/runtime/io/queue.hpp
carven/runtime/io/stdin.hpp
carven/runtime/numeric/float.hpp
carven/runtime/numeric/numeric.hpp
carven/runtime/numeric/parse.hpp
carven/runtime/simd/simd.hpp
carven/runtime/testing/testing.hpp
carven/runtime/text/text.hpp
carven/runtime/text/utf8.hpp
carven/runtime/value/deferred.hpp
carven/runtime/value/outcome.hpp
carven/runtime/value/stateless.hpp
carven/runtime/value/tuple.hpp
carven/runtime/value/void.hpp
```

The execution core owns cold operations, activation, host event sources, and the
driver. Child scopes own readiness, observation, and closure. Composition depends
on child scopes, time depends on the core, and timeout combines composition and
time. Each header includes its dependencies directly; generated files select the
header that owns each runtime symbol.

Use support headers matching the compiler that generated the artifacts.
Private target names, helper selections, and representation layouts are
implementation details.

`call/passing.hpp` supplies native parameter and transfer support;
`execution/entry.hpp` supplies process arguments and entry failure reporting.
`diagnostics/trap.hpp` supplies source-positioned termination for violated runtime
checks.
`testing/testing.hpp` supplies inline-test execution contexts, failure records,
and reporting in `carven::runtime`.

`text/text.hpp` supplies owning String, borrowed text views, UTF-8 validation,
scalar encoding and decoding, and native representation conversions. Its
implementation headers are `string.hpp`, `view.hpp`, and `utf8.hpp`.
`format/format.hpp` supplies general interpolation and requires C++20 `<format>`
support. `format/writer.hpp` defines FormatWriter for prepared builtin formatting,
including integer, floating, bool, char, and text fields; precomputed text uses
direct String construction or append. Structural printing uses
`display/display.hpp`. The consumer compiler checks delegated format strings
and native formatter availability. `io/print.hpp` supplies stdout and stderr
printing through the C++20 implementation or available C++23 library print
support under the consumer's selected standard.

For direct C++ calls, `String::from_str` and `append` require valid UTF-8, and
`push` requires a Unicode scalar. `String::from_utf8` validates incoming byte
storage and terminates on invalid UTF-8. Runtime String operations support C++20
constant evaluation. Carven constant execution retains String and Vector values
with their source types. The compiler emits immutable backing and native constexpr descriptors;
Read uses do not allocate. Runtime copies construct independent native owners.

## SIMD configuration

The C++20 runtime selects one SIMD backend per translation unit: AArch64 NEON
when available, x86 AVX2 when enabled by the consumer, and otherwise the portable
backend. All backends implement the same [logical lane contracts](../language/simd.md).

Consumers select target instruction flags. An x86 consumer enables AVX2 with
`-mavx2`, `/arch:AVX2`, or Xmake's `add_vectorexts("avx2")`.
`CARVEN_SIMD_FORCE_PORTABLE` selects the portable backend. Translation units using
SIMD or runtime text support must select the same backend.

Runtime vector and mask types live in backend-specific inline namespaces within
`carven::runtime::simd`. Their type identities can expose backend mismatches in
symbols that encode those types. Return types alone and enclosing structs may
not expose a mismatch at link time.
