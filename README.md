# Carven

> **The power of C++, in the palm of your hand.**

Carven is a programming language that compiles to C++20, combining expressive
syntax with checked ownership, typed failures, and compile-time capabilities.
It builds on your existing C++ libraries and toolchain, with zero-overhead
abstractions as a design goal.

## Why Carven?

### Intent over mechanism

Express whether an operation reads, mutates, or takes ownership of a value, with
explicit access choices for function arguments and closure captures. Carven
checks these contracts and manages the corresponding lifetimes. Write your intent
in source and let the compiler arrange C++ construction, evaluation, and cleanup.

### Compile-time capabilities

Build static data with familiar functions, loops, and text operations, and keep
constant functions available for runtime use. The compiler also uses known values,
types, and structure to precompute work and specialize runtime operations.
Even formatting dynamic values can benefit from prepared text, conversion choices,
and size information supplied by the compiler.

### Typed failure contracts

See what can fail in a function's contract. Propagate with `?` or recover with
patterns that give you the failure's type and payload. The same model extends to
callbacks, keeping failures visible as you compose operations. Choose recovery
where you have the context to handle it, with ownership and cleanup preserved.

### Zero-overhead abstractions

Use expressive language features with the cost of skilled handwritten C++ as the
design target. Carven uses known semantic facts to guide storage and native calls,
then puts your C++ compiler's optimizer to work. Generated C++ stays available for
inspection, so you can follow how your source becomes native code and measure it
with familiar performance tools.

### Seamless C++ interoperability

Bring C++ libraries into Carven through header imports, and make Carven functions
available to C++ through generated public interfaces. Reuse native types and APIs
alongside Carven code. Compile, link, and debug with your existing C++ tools and
build systems, and introduce Carven into a native project alongside existing code.

> [!NOTE]
> Carven is under active development, and language and tooling changes may
> break existing code. Use the compiler, documentation, and examples from the
> same revision.

## Build and run

Building the Carven compiler requires [Xmake](https://xmake.io/) and an
LLVM/Clang toolchain with C++26 support. The validated host toolchain is
LLVM/Clang and libc++ 23.1.0. Generated programs and support headers use C++20.

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
program entry and reports their results. Required constant evaluation and
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
The [CLI reference](docs/cli.md) defines execution limits and supported platforms.

`check`, `compile`, direct execution, and `interpret` all collect sources from
the toolchain's `crafts/carven/` and the working directory's `crafts/`. Application
sources outside those roots remain explicit, for example
`carven main.cv helpers.cv`. `CXX` selects the native compiler, defaulting to
`clang++`. See [source collection](docs/cli.md#source-collection) for the collected
roots and toolchain layout, and [native execution](docs/cli.md#native-execution)
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
    out/main.cpp out/crafts/carven/std/utf/*.cpp -o out/hello-carven
./out/hello-carven
```

Replace `/path/to/carven/crafts` with the installed directory beside the
toolchain's `bin/`, or use this repository's `crafts/` with the locally built
compiler. From the repository root, build the same file with the local compiler:

```shell
./xmakew run carven compile -o out/manual main.cv
clang++ -std=c++20 -Iout/manual -Icrafts \
    out/manual/main.cpp out/manual/crafts/carven/std/utf/*.cpp \
    -o out/manual/hello-carven
./out/manual/hello-carven
```

The commands above include the generated implementations for the bundled UTF
Craft. A `main.cv` that imports `std::utf.text` uses the same source collection;
there is no need to pass `/path/to/carven/crafts/carven/std/utf/*.cv` explicitly.
When additional Crafts are installed, include their generated implementations
and native providers in the C++ build as well.

Name imported application `.cv` files outside Crafts roots in the Carven
invocation. Supply any native `.cpp` files, include directories, and libraries
to the C++ toolchain.
The maintained Xmake rule discovers `.cv` and `.cpp` sources and adds include
roots for the toolchain and project `crafts/` directories; native library
dependencies remain ordinary build configuration. See the
[CLI reference](docs/cli.md) for source paths and generated artifact destinations.

## Documentation

- **Run examples:** [Learning examples](examples/README.md) and the
  [failure-contract example](examples/failures/README.md)
- **Learn the language:** [Tutorial](docs/tutorial.md),
  [Grammar](docs/grammar.md), and [Semantics](docs/semantics.md)
- **Use the compiler:** [CLI Reference](docs/cli.md) and
  [Toolchain and artifacts](docs/toolchain.md)
- **Understand the implementation:** [Compiler Architecture](docs/compiler.md)
  and [C++ Backend](docs/backend.md)
- **Develop the repository:** [Testing](docs/testing.md) and
  [C++ Conventions](docs/conventions.md), with
  [Xmake support](xmake/README.md)
- **Explore the design:** [Design Principles](docs/principles.md),
  [Proposals](proposals/), and [Learning Notes](notes/)

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

These commands use clang-format for C++ and [Graver](tools/graver/README.md)
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
