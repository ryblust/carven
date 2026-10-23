# Toolchain and artifacts

This reference defines native build requirements, generated artifact interfaces,
and build integration.

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

A successful build requires Carven analysis, native compilation, and linking.
Carven supplies C++ and artifact facts; the native compiler checks provider
contracts, definitions, overloads, templates, and delegated conversions. Generated
source mappings attribute C++ diagnostics to their Carven locations. Native checks
can fail after Carven analysis succeeds. For supported operations with satisfied
provider requirements, invalid generated C++ is a compiler defect.

### Construction limitation

An aggregate initializer may evaluate a native component before a later component
that can produce a Carven failure. If lowering saves the earlier component in
separate storage, final aggregate construction must copy or move it from that
storage. An immovable component cannot satisfy this generated construction,
even when C++ could construct it directly in the final aggregate.

C++ checks the required copy or move constructor. Direct construction from a
prvalue supports immovable results where no intermediate transfer is required.

## Numeric model

Carven `isize` and `usize` use the host's pointer-sized integer widths during
analysis and matching target types at runtime. Host and target data models must
match. `f32` and `f64` require IEEE 754 binary32 and binary64; runtime headers
check the target properties. Native options must preserve IEEE equality and
Carven evaluation order.

## Artifact paths

`check -o <dir>` writes retained semantic IR and `carven/manifest.json`. `compile` writes
C++ and its manifest. Each manifest indexes that operation's output artifacts.
Supplying an analysis directory to `compile` generates C++ from its published
program, including test generation, without repeating source analysis or static execution.
A checked Craft artifact directory can supply definitions to a new
source batch. The consuming analysis constructs its new generic and static
instances. The build system owns dependency freshness.

C++ artifact paths are:

```text
carven/manifest.json
carven/generated/<linkage-domain>/<component-anchor>.hpp
carven/api/<canonical-module-path>.hpp
carven/providers/<canonical-module-path>.hpp
<canonical-module-path>.cpp
carven/generated/carven-test-runner.hpp
carven/generated/carven-test-main.cpp
```

An interface component contains declarations connected by complete-definition
requirements. Its anchor is its first canonical module path. Modules without a
published semantic surface have no component of their own. Implementations use
canonical module paths. An implementation emits the static instances its module calls as `inline`
functions in the provider's namespace. New static arguments change the caller's
implementation; provider artifacts remain independent of their callers.

The `carven/api` header contains explicit `export(cpp)` declarations in
`carven::api`, followed by nested namespaces for the encoded module path
components. Its implementation contains the corresponding function façades. The
header is self-contained and publishes typed source contracts for its exported
callables.
Signature dependencies are collected regardless of their Carven visibility:
same-craft types need no ordinary `export` to be used through this C++ interface.

Modules containing concrete `import(cpp)` declarations produce a
`carven/providers` header with signature type aliases, typed host contracts, and
complete type dependencies. Imports without an explicit native target have a
matching `invoke` declaration supplied through their generated interface. Native
authors include the provider header when implementing those declarations.
Explicit targets retain authored C++ declarations.

C++ header imports become ordered includes in the owning implementation. An
external type or result query needed by an interface brings its context module's
complete header import list into that interface, including imports without
`using`. Module-scoped lookup additionally brings the complete using environment.
Each artifact materializes a module environment once, preserving that module's
import order, delimiter forms, and explicit repetitions. Different environments
follow canonical module-path order. Native headers must work in that order;
cross-module macro configuration order is not preserved. Raw source fragments remain
implementation-only and follow includes at global scope before generated namespaces.

Generated implementations include their associated interface component first,
when one exists. Generated test entries include their runner header first. Native
header environments follow, then runtime headers, other generated interfaces,
and directly required standard-library headers. Compiler-generated dependencies
are merged and sorted within their groups; explicit native occurrences remain
independent and retain their repetitions. Generated headers begin with pragma-once.

Test generation emits the runner header. `--tests` also emits a
main source; `--tests=runner` supplies the runner to a caller-provided
entry. Both use the same direct test functions and reporting protocol. The generated
runner invokes modules in canonical path order and tests within each module in
source order.

Project linkage identity combines the generation destination, canonical module
path, and test generation mode. Generated interface paths carry the same linkage
domain as their declarations. Each application target owns its application
generation destination; changing its source membership does not change that
destination. Each Craft has a stable linkage identity derived from its name;
standard and project Crafts use the same rule.

Public `export(cpp)` names belong to the program-wide `carven::api` namespace.
Give separately linked public modules distinct canonical paths. Reuse one library
target through native dependencies when multiple consumers need the same definitions.

Logical paths are relative to the output destination. The build system owns
stale artifact removal, installation, and native dependency scheduling.
The manifest projects the compilation result as JSON:

| Field | Meaning |
| --- | --- |
| `workspace.root` | Invocation working directory |
| `inputs` | Source paths, canonical module identities, ownership, and standard-library membership |
| `artifacts` | Relative output paths, roles, source mapping, and implementation source ownership |
| `runtime_test_count` | Runtime inline tests in owned modules, independently of emission mode |
| `cacheable` | Whether analysis completed without observing host input, time, or waiting |
| `include_dirs` | Absolute include roots for the toolchain and participating Crafts |
| `library_roots` | Directories containing available Craft source candidates |
| `craft_roots` | Participating Craft directories whose optional `build.cv` supplies declarations |
| `crafts` | Queried Craft project descriptions and their actual loaded script inputs |

Input paths within the workspace are relative; external paths are absolute.
Artifact roles are `interface`, `cpp-api-header`, `module-implementation`,
`test-runner-header`, `test-entry`, and `semantic-ir`. Source mapping is
`stable-interface` or `source-attributed`. Synthetic outputs have no source owner.
Native compiler dependency files establish C++ header dependencies.

A Craft uses an ordinary `build.cv` to describe its source groups and dependencies:

```carven
import std::build using project;

project({
    targets: [{
        name: "network",
        kind: .SourceLibrary,
        sources: ["net.cv", "net/native.cpp"],
        headers: ["net/native.hpp"],
        native: {
            syslinks: [{ platform: "windows", libraries: ["ws2_32"] }],
            ...
        },
        ...
    }],
    ...
})?;
```

`carven query` executes the declaration in its directory. `compile` includes the
queried project descriptions directly in its manifest; no authored JSON is needed.
Each query contains `project`, the declared model, and `scripts`, the absolute
paths of sources actually loaded to analyze that build entry, including transitive
imports and standard-library code. These paths come from the compiler's loaded
source inventory; declared target sources are not query inputs unless imported.
Build consumers can separate these stages with `compile --no-query`. Its manifest
retains `craft_roots` and leaves `crafts` empty. For each present `build.cv`, use
`check build.cv -o <checked>` and `query <checked>` in that Craft's directory.
The consumer checks analysis freshness from the checked manifest, then refreshes
the generated manifest's `crafts` with the query results. Queries always execute;
retained semantics skip only parsing, analysis, and prior static execution.
`SourceLibrary` declares sources and requirements without producing an archive.
A consuming native target compiles these sources with its own configuration.
Query target `kind` distinguishes `source-library`, `static-library`, and
`executable`; source dependency adapters consume only `source-library`.
Each source library has one `sources` list for Carven and native files, `headers` identifying
its interfaces, `dependencies` naming other targets, and `syslinks` grouped by
`all`, `windows`, `linux`, or `macos`. Paths in these descriptions are absolute.
The consumer selects libraries for its target platform, independently of the host
running the declaration.

Selected sources activate their declared source libraries and dependencies.
Carven files enter semantic analysis through imports
or explicit source roots; a declaration does not manufacture a generated implementation
for an unanalysed file. Such a selected file needs to be added to the semantic inputs.
Compiler flags, macros, optimization, and dependency acquisition remain the
consuming build target's responsibility. `SourceLibrary` does not impose native
compiler flags. Actual static-library targets are build products, not source
libraries to be flattened by an external adapter.

Analysis reuse follows `inputs` contents, the compiler, options, and candidate
file names under `library_roots`. Unused candidate contents do not invalidate an
analysis. Craft queries refresh independently of generation, so dynamic declarations
are not hidden behind analysis or generated-source reuse.
Native objects belong to their consuming build configuration; shared source or
generated text does not imply shared objects. A build system may reuse an object
only when its compiler, arguments, and input dependencies still match.

## Build integration

`carven compile` writes generated C++ and a manifest describing the selected
inputs, implementations, and link requirements. The consuming build system owns
native compiler settings, header dependencies, compilation, linking, and objects.
Importing a C++ header does not identify its implementation or link library;
the owning Craft declares these requirements in `build.cv`.

`carven build` executes the project's `build.cv`. Its `std::build` plan constructs
executables and static libraries. `carven run` launches an executable, and
`carven test` selects the plan's inline tests and builds their test programs.

External projects include the installed Xmake rule or set `Carven_DIR` and load
the installed CMake package. These adapters use the compiler and Crafts from that
installation. They translate the manifest into native source and link inputs;
projects keep their existing target declarations and need no project `build.cv`.

Xmake targets select Carven entry sources with `add_files`. Test targets explicitly
use `add_rules("carven", {tests = true})` and `add_tests`; the compiler emits all
inline tests in the selected sources. CMake targets use `carven_target_sources`
for source generation and ordinary CMake declarations for tests. A Carven
application entry is replaced in test mode; exclude an authored
C++ entry from a target that uses the generated test entry.

Xmake shares checked semantics for identical source batches and generated text for
identical generation requests. CMake retains these artifacts per target.
Native objects remain subject to their consuming target's
compiler settings and dependency rules. Separately linked definitions need one
owner; components exchanging generated types need compatible ABI settings.

Analysis tracks loaded sources and available module names. Analysis that observes
host effects runs again. Generation preserves unchanged text. Craft queries share
checked script semantics and execute afresh to observe external configuration;
changes to query results update native requirements independently of C++ generation.
The adapters use their build system's dependency and scheduling facilities.

The compiler repository binds its rule to the current compiler build and checkout
Crafts. Development starts with `./xmakew build`, followed by `./xmakew test`.
Installed toolchains do not participate in this workflow.

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
carven/runtime/io/print.hpp
carven/runtime/io/queue.hpp
carven/runtime/io/stdin.hpp
carven/runtime/io/stream.hpp
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

The I/O domain keeps platform byte-stream mechanisms in `io/stream.hpp`, input
request scheduling in `io/queue.hpp`, the native read entry in `io/stdin.hpp`, and
formatted printing in `io/print.hpp`. Platform differences stay within their
operation implementations.

Use support headers matching the compiler that generated the artifacts.
Private target names, helper selections, and representation layouts are
implementation details.

`call/passing.hpp` supplies native parameter and transfer support;
`execution/entry.hpp` supplies process arguments and entry failure reporting.
On Windows, it reads the CRT's wide argument vector and converts arguments to
UTF-8 before passing them to Carven. On POSIX, arguments passed to Carven must
already be valid UTF-8; invalid text violates the runtime contract and traps.
`diagnostics/trap.hpp` supplies source-positioned termination for violated runtime
checks.
`testing/testing.hpp` supplies inline-test execution contexts, failure records,
and reporting in `carven::runtime`. `testing/async.hpp` drives asynchronous test
roots through child closure and reports root cancellation to the active test.

`text/text.hpp` supplies owning String, borrowed text views, UTF-8 validation,
scalar encoding and decoding, and native representation conversions. Its
implementation headers are `string.hpp`, `view.hpp`, and `utf8.hpp`.
`format/format.hpp` supplies general interpolation and requires C++20 `<format>`
support. `format/writer.hpp` defines FormatWriter for prepared builtin formatting,
including integer, floating, bool, char, and text fields; precomputed text uses
direct String construction or append. Structural printing uses
`display/display.hpp`. The consumer compiler checks delegated format strings
and native formatter availability. `io/print.hpp` writes text to stdout and stderr
with C++20 `fwrite`; formatting and structural-display headers are selected by the
generated argument conversions.

For direct C++ calls, `String::from_str` and `append` require valid UTF-8, and
`push` requires a Unicode scalar. `String::from_utf8` validates incoming byte
storage and terminates on invalid UTF-8. Runtime String operations support C++20
constant evaluation. Retained Carven String and Vector constants use immutable
backing and native constexpr descriptors. Read uses do not allocate; runtime
copies construct independent owners.

## SIMD configuration

The C++20 runtime selects one SIMD backend per translation unit: AArch64 NEON
when available, x86 AVX2 when enabled by the consumer, and otherwise the portable
backend. All backends implement the same logical lane operations.

Consumers select target instruction flags. An x86 consumer enables AVX2 with
`-mavx2`, `/arch:AVX2`, or Xmake's `add_vectorexts("avx2")`.
`CARVEN_SIMD_FORCE_PORTABLE` selects the portable backend. Translation units
sharing SIMD or runtime text types and inline definitions must select the same
backend. Independent programs can select different backends.

Runtime vector and mask types live in backend-specific inline namespaces within
`carven::runtime::simd`. Their type identities can expose backend mismatches in
symbols that encode those types. Return types alone and enclosing structs may
not expose a mismatch at link time.
