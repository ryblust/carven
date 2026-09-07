# Learn by running

These small programs introduce Carven through complete tasks. Each directory
contains source, a short explanation, and expected output. Examples track the
compiler in this checkout; use source and compiler from the same revision.

## Build and run

From the repository root, with the [repository toolchain](../README.md#build-and-inspect)
installed:

```sh
./xmakew build
./xmakew build examples
./xmakew run example-receipt
./xmakew test -g examples
```

The first command builds the compiler. The second compiles the examples to
native executables; they use the local compiler and C++20. Replace
`example-receipt` with any target below. In PowerShell, use `.\xmakew.ps1`.
The examples are enabled with the repository's `build_tests` option (on by
default). Their executables are built explicitly, rather than by the default
build.

The build uses the same Carven Xmake rule as the repository's generated tests.
It may need to obtain that rule on initial configuration. For standalone C++
project setup, see [C++ project integration](../README.md#c-project-integration).
These directories share the repository build; they are not independent packages.

## Reading order

For a focused tour of failure contracts, start with the
[failure-contract series](failures/README.md).

| Direction | Program | Target | What to follow |
| --- | --- | --- | --- |
| Basics | [Receipt](basics/) | `example-receipt` | Values, records, arrays, loops, functions |
| Ownership | [Inventory](ownership/) | `example-inventory` | Read, Write, Take, copying and reassignment |
| Modules | [Shipping](modules/) | `example-shipping` | Relative imports and private constants |
| Failures | [Booking](failures/basic/) | `example-booking` | Failure payloads, propagation and handling |
| Composition | [Order quote](failures/composition/) | `example-order-quote` | Multiple failure types, guards and rethrow |
| Recovery | [Configuration](failures/recovery/) | `example-configuration` | Fallible recovery, translation and nested patterns |
| Callbacks | [Policies](failures/callbacks/) | `example-policies` | Inferred closure failures and callable widening |
| C++ calls | [Native parser](interop/import/) | `example-native-parser` | Header imports and native exception recovery |
| C++ host | [Pricing library](interop/export/) | `example-cpp-host` | Exported functions and generated public headers |

Start with each directory's `main.cv` (or `main.cpp` in the C++ host).
Change the small inputs in `main` and run the target again after rebuilding it.
The output check uses the documented inputs, so restore them before running the
example test group.

The Carven-entry examples use C++ console output. They import two C++ `print` overloads from [support/console.hpp](support/console.hpp):
one for text and one for i32. This shared helper only prints values; the task's
logic stays in the example. The native parser examines the same header-import
mechanism in more detail.

For an introduction to individual concepts, read the [tutorial](../docs/tutorial.md).
The [documentation index](../docs/README.md) lists language and tool references.
Examples use small, bounded inputs to keep each task focused.

## Maintaining examples

Keep source executable and explanations local to the program. Update affected
examples with language changes, then build and run the `examples` test group.
Its checks execute the actual programs and compare their output. Rejection and
termination cases belong in the compiler's test suites.
