# Carven

Carven is a high-level C++ platform with a clear division of responsibility:
Carven defines program meaning through a consistent language surface; C++
supplies native realization and a mature ecosystem.

## Why Carven?

### Intent over mechanism

C++ offers a rich set of mechanisms for precise control over representation and
behavior, setting the platform's capability ceiling. Carven uses those
mechanisms as realization choices rather than source obligations: source states
the operation, guarantee, or cost that matters, while the compiler selects the
C++ form that preserves the contract.

### Zero-overhead abstractions

Carven follows the zero-overhead principle: unused capabilities impose no
runtime cost, while used abstractions target the cost of skilled handwritten
C++ preserving the same guarantees. Zero overhead does not mean zero cost:
storage, checks, allocation, indirection, and dispatch remain when the requested
behavior requires them, but the abstraction itself introduces no incidental
runtime machinery. Compile-time facts disappear after the compiler has used
them, and the resulting C++ remains direct and optimizer-visible.

### Built-in C++ facilities

Carven lifts selected mature C++ facilities into coherent language-level
contracts. They become built-in Carven capabilities while remaining native C++
underneath, carrying forward the ecosystem's implementations and expertise.

### Seamless C++ interoperability

C++ is both Carven's native realization layer and its bridge to the wider
ecosystem. Libraries enter through explicit Carven declarations, while Carven
functions present explicit interfaces to C++ callers. Native tools and build
systems remain part of the same workflow, so Carven can be adopted module by
module inside existing C++ codebases without an all-at-once rewrite.

> [!NOTE]
> Carven is under active development. Current language and tooling contracts
> are documented below; the [roadmap](proposals/roadmap.md) tracks active and
> future design work.

## Build and inspect

Building Carven requires [Xmake](https://xmake.io/) and an LLVM/Clang toolchain
with C++26 support.

Use the repository wrapper for normal commands. It provides the versioned Xmake
and Clang module-build behavior expected by this repository. Use `./xmakew` on
POSIX systems and `.\xmakew.ps1` in Windows PowerShell. See the
[Clang module build pipeline](tools/xmake-clang-module-pipeline/README.md) for
implementation details and compatibility requirements.

Given a `main.cv` module, build the compiler and inspect its generated C++:

```shell
./xmakew build
./xmakew run carven --stdout main.cv
```

With no destination option, Carven writes generated artifacts below the current
directory. See the [CLI Reference](docs/cli.md) for source inputs, other output
modes, inspection commands, and test emission. Supported compiler hosts and
generated-C++ consumer modes are defined by
[Compatibility](docs/compatibility.md).

## C++ project integration

Carven runs as a source-generation step in a native C++ build. The maintained
Xmake package and rule live in the
[Carven Xmake Repository](https://github.com/ryblust/carven-xmake-repo). Other
build systems can invoke the compiler and consume its generated artifacts
according to the [CLI](docs/cli.md) and
[Compatibility](docs/compatibility.md) contracts.

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

The [Clang module build pipeline](tools/xmake-clang-module-pipeline/README.md)
documents the wrapper pipeline, its compatibility requirements, and the switch
to stock Xmake.
