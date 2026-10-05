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

## Build and run

Carven uses LLVM/Clang and libc++ across platforms. Building the compiler requires
[Xmake](https://xmake.io/) and a toolchain with C++26 support; LLVM 23 is the
validated version. Generated programs and support headers use C++20.

On Windows, use [LLVM-MinGW](https://github.com/mstorsjo/llvm-mingw). Add the LLVM
toolchain's `bin` directory to `PATH` so Xmake can discover the compiler and build
tools.

Use `./xmakew` on POSIX systems or `.\xmakew.ps1` in Windows PowerShell for
repository commands.

Save this Hello World as `main.cv` in the repository root. It uses the builtin
`println`:

```cv
// main.cv
println("Hello World");
```

From the repository root, build the compiler and run the example:

```shell
./xmakew build
./xmakew run carven main.cv
```

It prints `Hello World`. Bare source invocation compiles and runs a native program
using a native C++ toolchain.

A source file can also include tests. Save this as `demo.cv` in the same directory:

```cv
// demo.cv
fn twice(value: i32) -> i32 => value * 2;

test "twice" {
    check(twice(21) == 42);
}

println(twice(21));
```

Run the program or select its runtime tests using the same source file:

```shell
./xmakew run carven demo.cv
./xmakew run carven --tests demo.cv
./xmakew run carven interpret --tests demo.cv
```

The program prints `42`. With `--tests`, the driver runs the tests instead of the
program entry and reports their results. Compile-time execution and
`const test` still run during analysis. Add `--timings` to see time spent in each
command stage.

Use `check` for semantic checks and compile-time execution, `interpret` to run
the supported subset without native compilation, or `compile` to inspect or
retain C++ artifacts:

```shell
./xmakew run carven check main.cv
./xmakew run carven interpret main.cv
./xmakew run carven compile --stdout main.cv
```

The [execution example](examples/execution/README.md) combines top-level statements,
compile-time output, static tests, and ordinary function calls. `compile --stdout`
displays generated artifacts; `compile -o <dir>` writes them below the selected
directory. With no destination option, `compile` writes below the current directory.
The [CLI reference](docs/toolchain/cli.md) defines execution limits and supported platforms.

`check`, `compile`, direct execution, and `interpret` all collect sources from
the toolchain's `crafts/carven/` and the working directory's `crafts/`. Application
sources outside those roots remain explicit, for example
`carven main.cv helpers.cv`. `CXX` selects the native compiler, defaulting to
`clang++`. See [source collection](docs/toolchain/cli.md#source-collection) for the collected
roots and toolchain layout, and [native execution](docs/toolchain/cli.md#native-execution)
for process behavior.

## C++ project integration

Carven runs as a source-generation step in a native C++ build. The maintained
Xmake package and rule live in the
[Carven Xmake Repository](https://github.com/ryblust/carven-xmake-repo). Other
build systems can invoke Carven on a source batch, compile the generated C++,
and link it with their native providers.

### Compile generated C++ directly

Generated programs do not require Xmake. Invoke Carven with the application
`.cv` sources; installed Crafts are collected automatically. Then compile and
link the generated `.cpp` files with a C++20 or newer toolchain. Pass the generated
output directory and the Carven `crafts/` directory as C++ include roots.

Using the same `main.cv` from above, with an installed `carven` on `PATH`, run:

```shell
carven compile -o out main.cv
clang++ -std=c++20 -Iout -I/path/to/carven/crafts \
    out/main.cpp out/crafts/carven/std/utf/*.cpp \
    out/crafts/carven/std/simd/*.cpp -o out/hello-carven
./out/hello-carven
```

Replace `/path/to/carven/crafts` with the installed directory beside the
toolchain's `bin/`, or use this repository's `crafts/` with the locally built
compiler. From the repository root, build the same file with the local compiler:

```shell
./xmakew run carven compile -o out/manual main.cv
clang++ -std=c++20 -Iout/manual -Icrafts \
    out/manual/main.cpp out/manual/crafts/carven/std/utf/*.cpp \
    out/manual/crafts/carven/std/simd/*.cpp \
    -o out/manual/hello-carven
./out/manual/hello-carven
```

The commands above include the generated implementations for the bundled UTF
and SIMD Crafts. A `main.cv` that imports `std::utf.text` uses the same source
collection; there is no need to pass
`/path/to/carven/crafts/carven/std/utf/*.cv` explicitly.
When additional Crafts are installed, include their generated implementations
and native providers in the C++ build as well.

Name imported application `.cv` files outside Crafts roots in the Carven
invocation. Supply any native `.cpp` files, include directories, and libraries
to the C++ toolchain.
The maintained Xmake rule discovers `.cv` and `.cpp` sources and adds include
roots for the toolchain and project `crafts/` directories; native library
dependencies remain ordinary build configuration. See the
[CLI reference](docs/toolchain/cli.md) for source paths and generated artifact destinations.

## Documentation

- **Run examples:** [Learning examples](examples/README.md) and the
  [failure-contract example](examples/failures/README.md)
- **Learn the language:** [Tutorial](docs/language/tutorial.md),
  [Grammar](docs/language/grammar.md), and [Language Reference](docs/language/README.md)
- **Use the compiler:** [CLI Reference](docs/toolchain/cli.md) and
  [Toolchain and artifacts](docs/toolchain/artifacts.md)
- **Understand the implementation:** [Compiler Architecture](docs/compiler/README.md)
  and [C++ Backend](docs/compiler/backend/README.md)
- **Develop the repository:** [Testing](docs/development/testing.md) and
  [C++ Conventions](docs/development/conventions.md), with
  [Xmake support](xmake/README.md)
- **Explore the design:** [Design Principles](docs/development/principles.md),
  [Proposals](proposals/README.md), and [Learning Notes](notes/)

The [Documentation Index](docs/README.md) provides the complete guide to language,
toolchain, and development documentation.

## Development

The default build selects the compiler. Build it before running the test suite:

```shell
./xmakew build
./xmakew test
```

Format the repository's C++ and Carven sources, or check their formatting:

```shell
./xmakew format
./xmakew format-check
```

These commands use clang-format for C++ and [Formatter](tools/formatter/README.md)
for `.cv` files. `format` applies changes; `format-check` reports violations
without changing files.

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
