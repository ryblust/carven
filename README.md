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

The repository's Hello World uses the builtin `println`:

```cv
fn main() {
    println("Hello World");
}
```

From the repository root, build the compiler and run the example:

```shell
./xmakew build
./xmakew run carven examples/helloworld/main.cv
```

It prints `Hello World`. Bare source invocation compiles and runs a native program
using a native C++ toolchain. Use `interpret` to execute the supported semantic
subset without native compilation, or `compile` to inspect or retain C++ artifacts:

```shell
./xmakew run carven interpret examples/helloworld/main.cv
./xmakew run carven compile --stdout examples/helloworld/main.cv
```

The [execution example](examples/execution/README.md) combines top-level statements,
compile-time output, static tests, and ordinary function calls. `compile --stdout`
displays generated artifacts; `compile -o <dir>` writes them below the selected
directory. With no destination option, `compile` writes below the current directory.
The [CLI reference](docs/cli.md) defines execution limits and supported platforms.

Direct execution collects sources from the compiler's `crafts/carven/` and the
working directory's `crafts/`. Application sources remain explicit, for example
`carven main.cv helpers.cv`. `CXX` selects the native compiler, defaulting to
`clang++`. See [native execution](docs/cli.md#native-execution) for source discovery,
toolchain layout, and process behavior.

## C++ project integration

Carven runs as a source-generation step in a native C++ build. The maintained
Xmake package and rule live in the
[Carven Xmake Repository](https://github.com/ryblust/carven-xmake-repo). Other
build systems can invoke Carven on a source batch, compile the generated C++,
and link it with their native providers.

### Compile generated C++ directly

Generated programs do not require Xmake. Invoke Carven with every `.cv` source
in the compilation batch, then compile and link the generated `.cpp` files with
a C++20 or newer toolchain. Pass the generated output directory and the Carven
`crafts/` directory as C++ include roots.

For example, save the Hello World above as `main.cv`. With an installed `carven`
on `PATH`, run:

```shell
carven compile -o out main.cv
clang++ -std=c++20 -Iout -I/path/to/carven/crafts \
    out/main.cpp -o out/hello-carven
./out/hello-carven
```

Replace `/path/to/carven/crafts` with the installed directory beside the
toolchain's `bin/`, or use this repository's `crafts/` with the locally built
compiler. From the repository root, the existing example can be built directly:

```shell
./xmakew run carven compile -o out/manual examples/helloworld/main.cv
clang++ -std=c++20 -Iout/manual -Icrafts \
    out/manual/examples/helloworld/main.cpp -o out/manual/hello-carven
./out/manual/hello-carven
```

`carven compile` does not discover imported source modules. For a `main.cv` that imports
`std::utf.text`, explicitly include the package modules and their generated implementations:

```shell
carven compile -o out main.cv /path/to/carven/crafts/carven/std/utf/*.cv
clang++ -std=c++20 -Iout -I/path/to/carven/crafts \
    out/main.cpp out/crafts/carven/std/utf/*.cpp -o out/app
./out/app
```

Likewise, name imported project `.cv` files in the Carven invocation. Supply any
native `.cpp` files, include directories, and libraries to the C++ toolchain.
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

Use [Graver](tools/graver/README.md) to format `.cv` source files.

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
