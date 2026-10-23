# Carven

> **The power of C++, in the palm of your hand.**

Carven is a programming language that compiles to C++20, combining expressive
syntax with checked ownership, typed failures, and compile-time capabilities.
It builds on your existing C++ libraries and toolchain, with zero-overhead
abstractions as a design goal.

## Why Carven?

### Intent over mechanism

Express read access, mutation, and ownership transfer through function calls
and closure captures. Carven checks ownership and borrowed lifetimes across calls
and control flow, then arranges C++ construction, evaluation, and cleanup for you.

### Compile-time capabilities

Compute and validate values during compilation using familiar language constructs.
Reuse `const fn` functions at compile time and runtime under the same language
rules, and check results with `const test`.
Explicit static inputs and control flow select branches, expand loops, and
specialize functions before C++ generation. Generated calls carry only runtime
arguments.

### Typed failure contracts

See recoverable failures in a function's contract. Propagate with `?` or match
failure types and payloads to recover where you have the right context.
Carven infers failure sets for private functions and checks declared public
contracts. The same model keeps failures visible across functions and callbacks.

### Zero-overhead abstractions

Carven aims to offer expressive language features at the runtime cost of
handwritten C++ with the same behavior and safety guarantees. The compiler prepares
known parts of runtime operations and uses ownership and control-flow facts to
avoid unnecessary storage and checks. Value classes provide encapsulation without
implicit allocation or virtual dispatch. Inspect the generated C++ and measure
native performance with familiar tools.

### Seamless C++ interoperability

Reuse C++ types, templates, and libraries through header imports, and expose
Carven functions through generated C++ interfaces. Native calls use C++ overload
resolution and template deduction. Compile and debug with familiar native tools,
and adopt Carven incrementally within existing C++ projects.

> [!NOTE]
> Carven is under active development, and language and tooling changes may
> break existing code. Use the compiler, documentation, and examples from the
> same revision.

## Install

The first binary release is being prepared. Once published, install the latest
precompiled toolchain from GitHub Releases.

macOS and Linux:

```sh
curl -fsSL https://github.com/ryblust/carven/releases/latest/download/install.sh | sh
```

Commands are installed in `~/.local/bin`; add it to your `PATH`. Toolchain files
live in `${XDG_DATA_HOME:-$HOME/.local/share}/carven`.

Windows PowerShell:

```powershell
irm https://github.com/ryblust/carven/releases/latest/download/install.ps1 | iex
```

Add `%LOCALAPPDATA%\Programs\Carven\bin` to your user PATH.

For VS Code, download the release's `.vsix` and use **Extensions: Install from VSIX**.
The `carven-lang.carven` extension uses the installed toolchain; Node is not required.

## Run a program

Native execution requires a C++20 compiler, SDK, and linker in the environment.
`CXX` selects the compiler, defaulting to `clang++`. Interpretation and semantic
checking do not require native compilation.

Save this as `main.cv` in your working directory:

```cv
const fn twice(value: i32) -> i32 => value * 2;

test "twice" {
    check(twice(21) == 42);
}

const answer = twice(21);

println(answer);
```

Run the program from that directory:

```shell
carven run main.cv
```

It prints `42`. `answer` is computed during compilation. `carven run` compiles
and runs the program; no project configuration is required.

Run the same file's supported tests through interpretation:

```shell
carven interpret --tests main.cv
```

`interpret --tests` runs tests instead of the program entry. `const test` runs
during analysis.

Use `check` for semantic checks and compile-time execution, `interpret` to run
the supported subset without native compilation, or `compile` to inspect or
retain C++ artifacts:

```shell
carven check main.cv
carven interpret main.cv
carven compile --stdout main.cv
```

`compile --stdout` displays generated C++; `compile -o <dir>` writes its artifacts
below the chosen directory. Name all application sources in explicit-file commands.
Use `carven run main.cv helper.cv` to build and run explicit sources as one program.
Arguments after `carven run ... --` are passed to the program.

Format Carven sources through the same CLI:

```shell
carven format write main.cv
carven format check .
```

## Build a project

With `carven` on `PATH` and a C++20 compiler available, add this
`build.cv` beside the same `main.cv`:

```cv
import std::build using project;

project({
    targets: [{
        name: "app",
        kind: .Executable,
        sources: ["main.cv"],
        ...
    }],
    ...
})?;
```

Run from the project directory:

```shell
carven build
carven run app
```

This builds `build/app` (`build/app.exe` on Windows). `carven run app` builds and
runs it, printing `42`.

`carven build` interprets `build.cv` as an ordinary Carven program. Use functions,
conditions, and loops to construct the plan or run code generators.

## C++ project integration

To use Carven in an Xmake-managed C++ project, put the same `main.cv` in a new
directory and add this `xmake.lua`. Use Xmake 3.1.1 or later and a C++20-capable
toolchain:

```lua
includes("/path/to/carven/share/xmake/carven")

target("app")
    set_kind("binary")
    add_rules("carven")
    add_files("main.cv")
```

```shell
xmake build
xmake run app
```

`xmake run app` prints `42`. For inline tests, declare a binary target with
`add_rules("carven", {tests = true})` and register it with `add_tests`.

Replace `/path/to/carven` with your toolchain installation directory. The rules
invoke that installation's compiler.
Declare your Carven and C++ sources, include directories, and library dependencies
on the target. Xmake manages compilation, linking, and incremental builds.

For CMake 3.21 or newer, the installed toolchain provides a package:

```cmake
cmake_minimum_required(VERSION 3.21)
project(app LANGUAGES CXX)
set(Carven_DIR "/path/to/carven/share/cmake/Carven")
find_package(Carven CONFIG REQUIRED)

add_executable(app)
carven_target_sources(app main.cv)
```

Run `cmake -S . -B build`, then `cmake --build build`. CMake owns the target's
native settings and build outputs. Your project supplies the installation path
and prepares its build environment, locally and in CI. See the
[CMake integration](cmake/README.md) for configuration and incremental behavior.

### Compile generated C++ directly

Generated programs do not require Xmake. Invoke Carven with the application
`.cv` sources; installed Crafts are collected automatically. Then compile and
link the generated `.cpp` files and native providers with a C++20 or newer
toolchain. Pass the generated output directory and the Carven `crafts/` directory
as C++ include roots.

From your program directory, compile and run the same `main.cv`. Replace
`/path/to/carven/crafts` below with the toolchain's Crafts directory:

```shell
carven compile -o out main.cv
clang++ -std=c++20 -Iout -I/path/to/carven/crafts \
    out/main.cpp -o out/main
./out/main
```

To run its tests, generate with `--tests` and compile both the module and the
generated test entry. Test mode replaces the program entry:

```shell
carven compile --tests -o out/tests main.cv
clang++ -std=c++20 -Iout/tests -I/path/to/carven/crafts \
    out/tests/main.cpp out/tests/carven/generated/carven-test-main.cpp \
    -o out/tests/main-tests
./out/tests/main-tests
```

The installed toolchain places `crafts/` beside `bin/`. Use that directory as
the include root, or the compiler checkout's `crafts/` for a development build.

This example uses header-only runtime support. Programs using other modules or
native APIs must include the generated implementations and native providers
required by those dependencies.

Name imported application `.cv` files outside Crafts roots in the Carven
invocation. Supply any native `.cpp` files, include directories, and libraries
to the C++ toolchain.

## Documentation

- **Run examples:** [Complete example programs](examples/README.md)
- **Learn the language:** [Tutorial](docs/language/tutorial.md),
  [Grammar](docs/language/grammar.md), and [Language Reference](docs/language/README.md)
- **Use the compiler:** [CLI Reference](docs/toolchain/cli.md) and
  [Toolchain and artifacts](docs/toolchain/artifacts.md)
- **Build projects:** [Build scripts and API](docs/language/build.md)
- **Understand the implementation:** [Compiler Architecture](docs/compiler/README.md)
  and [C++ Backend](docs/compiler/backend/README.md)
- **Develop the repository:** [Testing](docs/development/testing.md),
  [C++ Conventions](docs/development/conventions.md), [Xmake support](xmake/README.md),
  [Editor development](editors/vscode/development.md), and [Distribution](tools/release/README.md)
- **Explore the design:** [Design Principles](docs/development/principles.md)
  and [Proposals](proposals/README.md)

The [Documentation Index](docs/README.md) provides the complete guide to language,
toolchain, and development documentation.

## Development

To build Carven from source, clone this repository. Building the compiler requires
Git, [Xmake](https://xmake.io/), and LLVM/Clang with libc++ and C++26 support;
LLVM 23 is the validated version. On Windows, use
[LLVM-MinGW](https://github.com/mstorsjo/llvm-mingw) and add its `bin` directory to
`PATH` so Xmake can discover the build tools. Generated programs use C++20.

Run repository build and test commands from the checkout root through `./xmakew`,
or `.\xmakew.ps1` in Windows PowerShell. The default build selects only the compiler.
Build it before running the test suite:

```shell
./xmakew build
./xmakew test
```

Repository tests use this checkout's compiler and Crafts. Build outputs are in
`build/<platform>/<arch>/<mode>/`. Run the compiler without installing it:

```shell
./xmakew run carven check examples/hello.cv
```

Build the remaining tools explicitly when using the checkout's LSP:

```shell
./xmakew build -g toolchain
```

Set VS Code's `carven.serverPath` to the built `carven-lsp` executable, then run
**Carven: Restart Language Server** after rebuilding.

Format the repository's C++ and Carven sources, or check their formatting:

```shell
./xmakew build carven-format
./xmakew format
./xmakew format-check
```

These commands use clang-format for C++ and `carven-format`
for `.cv` files. `format` applies changes; `format-check` reports violations
without changing files.

### Benchmarks

Workloads and measurement protocols live in [benchmarks/](benchmarks/README.md).
Build first, then run them through the repository wrapper:

```shell
./xmakew build
./xmakew bench compile
./xmakew bench incremental
./xmakew bench async
```

### Module build troubleshooting

If an unexpected compiler, module, BMI, dependency-order, or apparently
impossible type error occurs, clean and rebuild with the repository wrapper:

```shell
./xmakew clean
./xmakew build
```

If the wrapper cannot apply its versioned patch to the installed Xmake, use
stock Xmake to clean and rebuild:

```shell
xmake clean -a
xmake build
```

Clean the build tree before changing the compiler, toolchain, or build pipeline.
When the wrapper is usable, run `./xmakew clean -a` before switching to stock
Xmake. On Windows, use `.\xmakew.ps1` in place of `./xmakew`.
