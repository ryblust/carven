# Learn by running

These small programs demonstrate Carven through complete tasks. Each example
includes source, a reading guide, build commands, and expected output. Use the
examples and compiler from the same revision.

## Run your first example

With the [repository toolchain](../README.md#build-and-run) installed, run from
the repository root:

```sh
./xmakew build
./xmakew run carven examples/helloworld/main.cv
```

Expected output:

```text
Hello World
```

Direct execution builds and runs the program with the native C++ toolchain.
For multi-module examples, name each application `.cv` file; Crafts sources are
collected automatically. In PowerShell, use `.\xmakew.ps1`.

The targets below also support `./xmakew build <target>` and
`./xmakew run <target>`, including the C++ host example. They share the repository
build, use the local compiler, and compile as C++20; they are not standalone
packages. Initial configuration may need to obtain the Carven Xmake rule.

Each example occupies one directory with a single `main.cv` entry and any helper
modules beside it. Top-level statements form the implicit program entry. The C++
host example uses `main.cpp` as its process entry and calls exported Carven functions.

## Choose an example

Read in table order for a progression through modules, values, ownership,
constant computation, failures, and C++ integration, or choose a topic directly.

| Example | What it demonstrates | Target |
| --- | --- | --- |
| [Hello World](helloworld/) | Top-level entry, string literal, and builtin output | `carven-example-hello-world` |
| [Shipping](modules/) | Relative imports and private constants | `carven-example-shipping` |
| [Receipt](basics/) | Records, enums, match, arrays, loops, and calculation functions | `carven-example-receipt` |
| [Inventory](ownership/) | Read, Write, Take, copying and reassignment | `carven-example-inventory` |
| [Greeting](strings/) | UTF-8 String, independent copies, and scoped borrowing | `carven-example-strings` |
| [Execution stages](execution/) | Constant blocks, static tests, native execution, and interpretation | `carven-example-execution` |
| [Constant computation](constant/) | Text construction, fixed arrays, frozen slices, and typed record tables, with ordinary runtime calls | `carven-example-constant` |
| [Failure contracts](failures/) | Expression propagation, recovery, callable contracts, and owned or borrowed failure payloads | `carven-example-failures` |
| [Native parser](call_cpp/) | Header imports, `#[cpp]`, and native exceptions converted to declared failures | `carven-example-native-parser` |
| [Pricing library](cpp_host/) | Generated public headers, owning String results, Write, and Take | `carven-example-cpp-host` |

Within each example, follow its reading guide from declarations and providers
to helpers and callers. Change the small inputs, rebuild, and run to observe the
result. Restore the documented inputs before checking expected output.

For an introduction to individual language concepts, use the
[tutorial](../docs/tutorial.md).

## Check the examples

Build all examples and run their output checks from the repository root:

```sh
./xmakew build examples
./xmakew test -g examples
```

The test group also builds its targets as needed. Its checks execute the actual
programs and compare their output with the expected results.

When updating an example, keep its explanation, commands, and expected output
consistent with the executable source. Explain providers before callers and
keep navigation between examples in this index. Rejection and termination cases
belong in the compiler's test suites.
