# Carven

Carven is an experimental source language that turns program intent into
compiler-checked semantic contracts, then translates `.cv` modules into
ordinary C++. The downstream C++ toolchain remains responsible for compiling,
optimizing, and linking the generated program.

## Why Carven?

### Failures are part of the contract

Recoverable failures are visible in callable contracts. Non-private functions
declare the closed set of failures they may produce, while private functions
and lambdas can infer that set from the operations they compose. Every pending
failure must be handled or explicitly propagated.

```carven
struct ReadFailure {
    code: i32,
}

struct ParseFailure {
    offset: i32,
}

fn read(available: bool) -> i32 throw ReadFailure {
    if !available {
        throw ReadFailure { code: 404 };
    }
    return 42;
}

fn parse(source: i32, valid: bool) -> i32 throw ParseFailure {
    if !valid {
        throw ParseFailure { offset: source };
    }
    return source;
}

private fn load(available: bool, valid: bool) -> i32 {
    return parse(read(available)?, valid)?;
}

fn load_config(
    available: bool,
    valid: bool,
) -> i32 throw ReadFailure + ParseFailure {
    return load(available, valid)?;
}

fn main() {
    let _ = try {
        load_config(true, false)?
    } catch {
        ReadFailure(error) => error.code,
        ParseFailure(error) => error.offset,
    };
}
```

Here, `read` and `parse` expose their individual contracts. The private `load`
function composes them without repeating either failure type; the compiler
infers the combined set. `load_config` declares that set, and `main` handles
both failure types. See [Language](docs/language.md) for the supported language
surface and
[Semantics](docs/semantics.md#failure-contracts) for the precise contract.

## Build and inspect

Building Carven requires [Xmake](https://xmake.io/) and an LLVM/Clang toolchain
with C++26 support. Save the example above as `main.cv`, then build the compiler
and inspect its generated C++:

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
