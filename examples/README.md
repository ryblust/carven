# Learn by running

These small programs demonstrate Carven through complete tasks. Each example
includes source, a reading guide, build commands, and expected output. Use the
examples and compiler from the same revision.

## Run your first example

With the [repository toolchain](../README.md#build-and-run) installed, run from
the repository root:

```sh
./xmakew build
./xmakew build carven-example-hello-world
./xmakew run carven-example-hello-world
```

Expected output:

```text
Hello World
```

Replace `carven-example-hello-world` in the build and run commands with a target
below. In PowerShell, use `.\xmakew.ps1`. The examples share the repository build,
use the local compiler, and compile as C++20; they are not standalone packages.
Initial configuration may need to obtain the Carven Xmake rule.

Each example occupies one directory with a single `main.cv` entry and any helper
modules beside it. The C++ host example uses `main.cpp` as its process entry and
calls exported Carven functions.

## Choose an example

Read in table order for a progression through modules, values, ownership,
constant computation, failures, and C++ integration, or choose a topic directly.

| Example | What it demonstrates | Target |
| --- | --- | --- |
| [Hello World](helloworld/) | Entry point, string literal, and builtin output | `carven-example-hello-world` |
| [Shipping](modules/) | Relative imports and private constants | `carven-example-shipping` |
| [Receipt](basics/) | Records, enums, match, arrays, loops, and calculation functions | `carven-example-receipt` |
| [Inventory](ownership/) | Read, Write, Take, copying and reassignment | `carven-example-inventory` |
| [Greeting](strings/) | UTF-8 String, independent copies, and scoped borrowing | `carven-example-strings` |
| [Constant computation](constant/) | Text construction, fixed arrays, frozen slices, and typed record tables, with ordinary runtime calls | `carven-example-constant` |
| [Failure contracts](failures/) | Expression propagation, recovery, callable contracts, and owned or borrowed failure payloads | `carven-example-failures` |
| [Native parser](call_cpp/) | Header imports and native exception recovery | `carven-example-native-parser` |
| [Pricing library](cpp_host/) | Exported functions and generated public headers | `carven-example-cpp-host` |

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

[Execution stages](execution/README.md) demonstrates top-level statements,
compile-time output and tests, native execution, and interpretation of the same program.
