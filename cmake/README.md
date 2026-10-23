# CMake integration

The installed toolchain includes `share/cmake/Carven/CarvenConfig.cmake`.
Use CMake 3.21 or newer and a C++20-capable native toolchain:

```cmake
cmake_minimum_required(VERSION 3.21)
project(example LANGUAGES CXX)

set(Carven_DIR "/path/to/carven/share/cmake/Carven")
find_package(Carven CONFIG REQUIRED)
add_executable(example main.cpp)
carven_target_sources(example bridge.cv)
```

```sh
cmake -S . -B build
cmake --build build
```

Replace `/path/to/carven` with your toolchain installation directory. The project
supplies this path and prepares its native build environment, locally and in CI.

Call `carven_target_sources` once per target with its complete Carven entry list.
Paths are relative to the calling `CMakeLists.txt`; module resolution uses
`PROJECT_SOURCE_DIR`. The package invokes the compiler from its own installation.
It does not download or build a toolchain, or require a project `build.cv`.

Configuration generates C++ and consumes the manifest's implementation links.
Reached sources select their unique Craft source libraries, including declared
sources, dependencies, and target-platform system libraries. Unselected Craft
files are not compilation roots. A selected Carven file must be imported or
listed explicitly so its implementation exists. Native headers do not select
source libraries. Express C++-only dependencies with ordinary `target_sources`
and `target_link_libraries` calls.

CMake owns native compilation, flags, macros, object files, and linking. Generated
interface include paths and the C++20 minimum propagate through ordinary target
dependencies, including when the consumer is a static library. Implementation
sources belong to that target.

Analysis retains the compiler's SemIR. Generation reuses that artifact and runs
only when its inputs change or outputs are missing. Both stages use the same
manifest dependencies and CMake file freshness checks. Unchanged generated files
retain their timestamps; obsolete artifacts are removed. Each target keeps its
own native compilation context.

Targets share checked Craft build scripts. Queries execute on every configuration
without caching their results. Loaded sources and imported build helpers trigger
automatic reconfiguration; candidate filename changes use `CONFIGURE_DEPENDS`.
Changes to external environment or non-source query inputs require running
`cmake -S . -B build` again, following CMake's configuration model.

The repository itself continues to build with Xmake. This directory supplies the
installed CMake adapter, not a second build definition for the compiler.
