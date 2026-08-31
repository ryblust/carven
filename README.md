# Carven

Carven is a high-level C++ platform built around a clear division of
responsibility: Carven defines program meaning through a consistent language
surface; C++ supplies native realization and a mature ecosystem.

## Why Carven?

### Intent over mechanism

C++ offers a rich set of mechanisms for precise control over representation and
behavior, setting the platform's capability ceiling. Carven uses those
mechanisms as realization choices rather than source obligations: source states
the operation, guarantee, or cost that matters, while the compiler selects the
C++ form that preserves the contract.

### Built-in C++ facilities

Carven lifts mature C++ facilities into coherent language-level contracts. They
become built-in Carven capabilities while remaining native C++ underneath,
carrying forward the ecosystem's implementations and expertise.

### Seamless C++ interoperability

C++ is both Carven's native realization layer and its bridge to the wider
ecosystem. Libraries enter through ordinary Carven declarations, while Carven
functions present explicit interfaces to C++ callers. Native tools and build
systems remain part of the same workflow.

Carven is under active development. See the
[roadmap](proposals/roadmap.md) for current and future design work.

## Build and inspect

Building Carven requires [Xmake](https://xmake.io/) and an LLVM/Clang toolchain
with C++26 support. Given a `main.cv` module, build the compiler and inspect its
generated C++:

```shell
./xmakew build
./xmakew run carven --stdout main.cv
```

With no destination option, Carven writes generated artifacts below the current
directory. Each invocation compiles the complete module batch supplied to it.
The [CLI Reference](docs/cli.md) documents source inputs, other output modes,
inspection commands, and test emission. Supported compiler hosts and
generated-C++ consumer modes are defined by the
[Compatibility](docs/compatibility.md) contract.

## C++ project integration

Carven runs as a source-generation step in a native C++ build. The maintained
Xmake package and rule live in the
[Carven Xmake Repository](https://github.com/ryblust/carven-xmake-repo). Other
build systems can invoke the compiler and consume its generated artifacts
according to the [CLI](docs/cli.md) and
[Compatibility](docs/compatibility.md) contracts.

Custom integrations identify each logical generation target with a stable,
nonempty `--linkage-domain`. The [CLI reference](docs/cli.md#linkage-domain)
defines the default and uniqueness requirements.

## Documentation

- **Learn the language:** [Language](docs/language.md),
  [Grammar](docs/grammar.md), and [Semantics](docs/semantics.md)
- **Use the compiler:** [CLI Reference](docs/cli.md) and
  [Compatibility](docs/compatibility.md)
- **Understand the implementation:** [Compiler Architecture](docs/compiler.md)
  and [C++ Backend](docs/backend.md)
- **Develop the repository:** [Testing](docs/testing.md) and
  [C++ Conventions](docs/conventions.md)
- **Explore the design:** [Design Philosophy](docs/philosophy.md),
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

On Windows, use `.\xmakew.ps1`. [Testing](docs/testing.md) defines the test
groups and validation workflow, while [C++ Conventions](docs/conventions.md)
defines repository source rules.

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

The [Clang module build pipeline](tools/xmake-clang-module-pipeline/README.md)
documents the wrapper pipeline, its compatibility requirements, and the switch
to stock Xmake.
