# Carven

> **The power of C++, in the palm of your hand.**

Carven is a programming language that generates inspectable C++ and fits into
existing C++ projects, toolchains, and build systems. It makes ownership,
access, and failure contracts part of the language, while the compiler selects
the C++ representation that preserves them.

## Why Carven?

### Intent over mechanism

Express whether an operation reads, mutates, or takes ownership of a value.
Carven checks those distinctions and manages the corresponding lifetimes and
C++ operations. Ownership transfers, mutable access, and closure captures stay
visible in source.

### Typed failure contracts

Failure types are part of a function's contract, alongside its successful
result. Combine operations and preserve their distinct failure types and
payloads. Private helpers and lambdas can infer their failure sets; shared
interfaces declare bounds checked by the compiler. Propagate with `?`, recover
with patterns, or translate failures at an interface. These contracts also
apply to callbacks.

### Zero-overhead abstractions

Carven follows the zero-overhead principle, targeting the cost of skilled
handwritten C++ with the same guarantees. Compile-time distinctions need no
runtime representation unless execution requires it. Storage, checks, and
dispatch serve the requested behavior, and generated C++ remains available
for inspection and optimization.

### Built on the C++ ecosystem

Carven aims to build on the C++ community's mature libraries and expertise,
bringing established capabilities into the language as built-in facilities.

### Seamless C++ Interoperation

Import C++ types and functions from headers, and export Carven functions through
generated public interfaces. C++ checks native declarations and operations;
Carven checks its own ownership, access, and failure contracts. Existing native
tools and build systems compile and link the generated code.

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

From the repository root, build the compiler and inspect the generated C++:

```shell
./xmakew build
./xmakew run carven --stdout examples/helloworld/main.cv
```

Build and run the same example as a native executable:

```shell
./xmakew build carven-example-hello-world
./xmakew run carven-example-hello-world
```

It prints `Hello World`. The Carven CLI generates C++; the build system compiles
and links it. `--stdout` displays generated artifacts for inspection. To write
them to a directory, use `-o <dir>`; without a destination option, the CLI writes
below the current directory.

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
carven -o out main.cv
clang++ -std=c++20 -Iout -I/path/to/carven/crafts \
    out/main.cpp -o out/hello-carven
./out/hello-carven
```

Replace `/path/to/carven/crafts` with the installed directory beside the
toolchain's `bin/`, or use this repository's `crafts/` with the locally built
compiler. From the repository root, the existing example can be built directly:

```shell
./xmakew run carven -o out/manual examples/helloworld/main.cv
clang++ -std=c++20 -Iout/manual -Icrafts \
    out/manual/examples/helloworld/main.cpp -o out/manual/hello-carven
./out/manual/hello-carven
```

Carven does not discover imported source modules. For a `main.cv` that imports
`std::utf.text`, explicitly include the package modules and their generated implementations:

```shell
carven -o out main.cv /path/to/carven/crafts/carven/std/utf/*.cv
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
  [failure-contract series](examples/failures/README.md)
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
