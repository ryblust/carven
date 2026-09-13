# Learn by running

These small programs introduce Carven through complete tasks. Each directory
contains source, a short explanation, and expected output. Examples track the
compiler in this checkout; use source and compiler from the same revision.

## Build and run

From the repository root, with the [repository toolchain](../README.md#build-and-run)
installed:

```sh
./xmakew build
./xmakew build examples
./xmakew run carven-example-receipt
./xmakew test -g examples
```

The first command builds the compiler. The second compiles the examples to
native executables; they use the local compiler and C++20. Replace
`carven-example-receipt` with any target below. In PowerShell, use `.\xmakew.ps1`.
Examples are built explicitly or by `test -g examples`.

The build uses the same Carven Xmake rule as the repository's generated tests.
It may need to obtain that rule on initial configuration. For standalone C++
project setup, see [C++ project integration](../README.md#c-project-integration).
These directories share the repository build; they are not independent packages.

## Reading order

The sequence starts with an entry point and output, then modules, declarations and values,
access, and composed behavior. The failure-contract series follows the same
progression from providers to their callers.

| Direction | Program | Target | What to follow |
| --- | --- | --- | --- |
| First program | [Hello World](helloworld/) | `carven-example-hello-world` | Entry point, string literal, and builtin output |
| Modules | [Shipping](modules/) | `carven-example-shipping` | Relative imports and private constants |
| Basics | [Receipt](basics/) | `carven-example-receipt` | Values, records, arrays, loops, functions |
| Ownership | [Inventory](ownership/) | `carven-example-inventory` | Read, Write, Take, copying and reassignment |
| Owning text | [Greeting](strings/) | `carven-example-strings` | UTF-8 String, independent copies, and scoped borrowing |
| Failures | [Booking](failures/basic/) | `carven-example-booking` | Failure payloads, propagation and handling |
| Composition | [Order quote](failures/composition/) | `carven-example-order-quote` | Multiple failure types, guards and rethrow |
| Recovery | [Configuration](failures/recovery/) | `carven-example-configuration` | Fallible recovery, translation and nested patterns |
| Callbacks | [Policies](failures/callbacks/) | `carven-example-policies` | Inferred closure failures and callable widening |
| C++ calls | [Native parser](interop/cpp_calls/) | `carven-example-native-parser` | Header imports and native exception recovery |
| C++ host | [Pricing library](interop/cpp_host/) | `carven-example-cpp-host` | Exported functions and generated public headers |

Within a program, read imports and provider modules, then type and constant
declarations, helper functions, and the entry point. Each directory gives the
file order. Change the small inputs in `main` and run the target after rebuilding.
The output check uses the documented inputs, so restore them before running the
example test group.

Carven-entry examples use builtin `println` for output. It accepts scalar and
text values without imports, separates arguments with spaces, and appends a
newline. For example, `println("Total in cents:", total)` prints a label and
value on one line. The native parser demonstrates
C++ header imports; the pricing library demonstrates a C++ host.

For an introduction to individual concepts, read the [tutorial](../docs/tutorial.md).
The [documentation index](../docs/README.md) lists language and tool references.
Examples use small, bounded inputs to keep each task focused.

## Maintaining examples

Keep source executable and explanations local to the program. Explain declarations
before their uses and providers before callers. Put build commands and expected
output after the source explanation. Keep cross-example navigation in this index.
Update affected
examples with language changes, then build and run the `examples` test group.
Its checks execute the actual programs and compare their output. Rejection and
termination cases belong in the compiler's test suites.
