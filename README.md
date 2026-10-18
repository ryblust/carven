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

## Run a program

Use `carven` with its matching Crafts resources. An installed toolchain places
the executable in `<prefix>/bin/` and its resources in `<prefix>/crafts/carven/`.
Keep development executables in their source checkout so they can locate its
`crafts/carven/`. For a source build, see [Development](#development).

Add the directory containing `carven` (`carven.exe` on Windows) to `PATH`.
Replace the example path with that directory's absolute path.

On macOS and Linux:

```sh
export PATH="/absolute/path/to/compiler-directory:$PATH"
```

In Windows PowerShell:

```powershell
$env:Path = "C:\path\to\compiler-directory;$env:Path"
```

These changes apply to the current terminal. To keep them for future terminals,
add the directory in your shell startup configuration or Windows user `Path`.
Check that the command is available:

```sh
carven --version
```

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
carven main.cv
```

It prints `42`. `answer` is computed during compilation. Bare source invocation
compiles and runs a native program using a native C++ toolchain.

Run the same file's tests natively:

```shell
carven --tests main.cv
```

With `--tests`, the driver runs the tests instead of the program entry.
Compile-time execution and `const test` still run during analysis.
Add `--timings` to see time spent in each command stage.

Use `check` for semantic checks and compile-time execution, `interpret` to run
the supported subset without native compilation, or `compile` to inspect or
retain C++ artifacts:

```shell
carven check main.cv
carven interpret main.cv
carven compile --stdout main.cv
```

`compile --stdout` displays generated C++; `compile -o <dir>` writes the artifacts
to a directory. Commands collect installed and project `crafts/` sources
automatically. Name other application files explicitly.
Direct execution uses `CXX`, defaulting to `clang++`;
arguments after `--` are passed to the program.

## C++ project integration

For a project build, put the same `main.cv` in a new directory and add this
`xmake.lua`. Use Xmake 3.1.1 or later and a C++20-capable toolchain:

```lua
add_repositories("carven-xmake-repo https://github.com/ryblust/carven-xmake-repo.git")
add_requires("carven")

target("app")
    set_kind("binary")
    add_rules("@carven/carven")
    add_files("main.cv")
```

```shell
xmake build
xmake run app
xmake test
```

`xmake run app` prints `42`; `xmake test` builds and runs the `twice` test from
`main.cv`.

The package installs the compiler. The rule runs Carven analysis and C++ generation,
sharing semantic results and generated C++ for the full installed standard library
within the configured project. It registers the sources on your target; Xmake
manages native compilation, linking, and incremental builds. Add your C++ sources,
include directories, and library dependencies with ordinary Xmake settings.

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

- **Run examples:** [Complete example programs](examples/README.md), including
  [async TCP collection](examples/stream_collector/README.md)
- **Learn the language:** [Tutorial](docs/language/tutorial.md),
  [Grammar](docs/language/grammar.md), and [Language Reference](docs/language/README.md)
- **Use the compiler:** [CLI Reference](docs/toolchain/cli.md) and
  [Toolchain and artifacts](docs/toolchain/artifacts.md)
- **Understand the implementation:** [Compiler Architecture](docs/compiler/README.md)
  and [C++ Backend](docs/compiler/backend/README.md)
- **Develop the repository:** [Testing](docs/development/testing.md) and
  [C++ Conventions](docs/development/conventions.md), with
  [Xmake support](xmake/README.md)
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
or `.\xmakew.ps1` in Windows PowerShell. The default build selects the compiler.
Build it before running the test suite:

```shell
./xmakew build
./xmakew test
```

The compiler is written to `build/<platform>/<arch>/<mode>/` by default.
Run `./xmakew show -t carven` (`.\xmakew.ps1 show -t carven` on Windows) to find
the `targetfile` for your configuration. Add its containing directory to `PATH`
as shown in [Run a program](#run-a-program). Keep the executable in the checkout
so it can locate the matching Crafts resources.

Format the repository's C++ and Carven sources, or check their formatting:

```shell
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
