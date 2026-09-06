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
visible in source. See the [ownership example](examples/ownership/).

### Typed failure contracts

Failure types are part of a function's contract, alongside its successful
result. Combine operations and preserve their distinct failure types and
payloads. Private helpers and lambdas can infer their failure sets; shared
interfaces declare bounds checked by the compiler. Propagate with `?`, recover
with patterns, or translate failures at an interface. These contracts also
apply to callbacks. Explore the [failure-contract examples](examples/failures/README.md).

### Zero-overhead abstractions

Carven follows the zero-overhead principle, targeting the cost of skilled
handwritten C++ with the same guarantees. Compile-time distinctions need no
runtime representation unless execution requires it. Storage, checks, and
dispatch serve the requested behavior, and generated C++ remains available
for inspection and optimization. The [design principles](docs/principles.md#cost-follows-behavior)
define this cost model.

### Built-in C++ facilities

Carven gives selected C++ facilities a language-level form. Arrays, iteration,
and callable views have Carven contracts backed by native C++ implementations.
The compiler supplies the supporting code, so these facilities fit the same
ownership and access rules as the rest of the language.

### Seamless C++ interoperability

Import C++ types and functions from headers, and export Carven functions through
generated public interfaces. C++ checks native declarations and operations;
Carven checks its own language contracts. Existing native tools and build
systems support gradual adoption inside a C++ project. Try
[calling C++](examples/interop/importing/) or
[using Carven from C++](examples/interop/exporting/).

> [!NOTE]
> Carven is under active development, and language and tooling changes may
> break existing code. Use the compiler, documentation, and examples from the
> same revision. The [roadmap](proposals/roadmap.md) tracks design work.

## Build and inspect

Building Carven requires [Xmake](https://xmake.io/) and an LLVM/Clang toolchain
with C++26 support.

Use the repository wrapper for normal commands. It provides the versioned Xmake
and Clang module-build behavior expected by this repository. Use `./xmakew` on
POSIX systems and `.\xmakew.ps1` in Windows PowerShell. See the
[Clang module build pipeline](xmake/clang-module-pipeline/README.md) for
implementation details and compatibility requirements.

Given a `main.cv` module, build the compiler and inspect its generated C++:

```shell
./xmakew build
./xmakew run carven --stdout main.cv
```

With no destination option, Carven writes generated artifacts below the current
directory. See the [CLI Reference](docs/cli.md) for source inputs, other output
modes, inspection commands, and test emission. Supported compiler hosts and
generated-C++ requirements are defined by
[Toolchain and artifacts](docs/toolchain.md).

## C++ project integration

Carven runs as a source-generation step in a native C++ build. The maintained
Xmake package and rule live in the
[Carven Xmake Repository](https://github.com/ryblust/carven-xmake-repo). Other
build systems can invoke the compiler and consume its generated artifacts
according to the [CLI](docs/cli.md) and
[Toolchain and artifacts](docs/toolchain.md) contracts.

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
  [C++ Conventions](docs/conventions.md)
- **Explore the design:** [Design Principles](docs/principles.md),
  [Proposals](proposals/), and [Learning Notes](notes/)

The [Documentation Index](docs/README.md) maps the permanent contracts,
maintainer references, and repository policies.

## Development

Generated-program tests use the locally built `carven` executable. Build and
test are separate invocations because the executable must exist before Xmake's
prepare-stage generation and named-module scanning begin:

```shell
./xmakew build
./xmakew test
```

[Testing](docs/testing.md) defines the test groups and validation workflow,
while [C++ Conventions](docs/conventions.md) defines repository source rules.

### Module build troubleshooting

If an unexpected compiler, module, BMI, dependency-order, or apparently
impossible type error occurs, clean and rebuild with the repository wrapper:

```shell
./xmakew clean
./xmakew build
```

If the clean wrapper build still fails, try building with stock Xmake:

```shell
./xmakew clean -a
xmake build
```

The [Clang module build pipeline](xmake/clang-module-pipeline/README.md)
documents the wrapper pipeline, its compatibility requirements, and the switch
to stock Xmake.
