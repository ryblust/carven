# Hosting library

`carven-hosting` embeds Carven source execution in C++ applications. Hosts include
the C++23 headers under `carven/hosting/` and link the static library using a
compatible C++ toolchain and standard library. The archive contains the required
compiler implementation; consumers do not need compiler module imports or BMIs.
Release archives contain native object code and do not require LLVM LTO support
in the consumer's linker.

The toolchain installs the platform's static-library filename under `lib/`, public
headers under `include/carven/hosting/`, and runtime header dependencies under
`include/carven/runtime/`. Add the installation's
`include/` directory to the host's include paths. The separate `crafts/` directory
contains Carven source libraries and their native support.

Hosts explicitly bind the native operations and application services their source
uses. `host_standard_services()` from `<carven/hosting/standard.hpp>` supplies an
optional set of standard bindings. Sessions use only the bindings supplied at
creation. The host target also links its application providers and any generated
entries; loading source does not load native libraries or infer bindings.

Bindings admit runtime calls. Static calls require a binding marked with
`.with_static_execution(...)`; environment operations also require the host's
effect permission. Session load and replacement use these same bindings.

`HostProgram` loads source for interpreted calls without native providers. Use
`HostSession::load` when loading source requires host bindings. `HostSession` owns
selected entries, shared native bindings, tasks, and events. Loading and
replacement can receive a source batch and a dependency resolver. A successful
replacement updates selected entries atomically; admitted calls retain their original
implementation, and a failed replacement leaves the current entries unchanged.
The host controls polling, event delivery, cancellation, and which completed results
it publishes.

Public headers live in `tools/hosting/include/carven/hosting/`; all SDK adaptation
lives in `tools/hosting/src/`. The static library depends on `carven-modules`.
CLI and workspace tools use core internal contracts without depending on the SDK;
the default compiler build does not build the SDK.

Build from the repository root, then explicitly build and install the SDK:

```sh
./xmakew build
./xmakew build carven-hosting
./xmakew install -o "$PWD/build/toolchain" carven-hosting
```

Use `.\xmakew.ps1` with the same arguments on Windows. The `toolchain` build group
also includes the library.
