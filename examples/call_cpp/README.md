# Adapt a C++ parser

The global `parse_port` function in `parser.hpp` calls `std::stoi`, checks
complete consumption and the port range, and throws C++ exceptions for invalid
input. It retains `std::stoi`'s acceptance of leading whitespace and a plus sign.
This header has no Carven dependency.

In `main.cv`, the header import supplies the native declarations. A `#[cpp]`
fragment defines
`parse_port_native`, which catches `invalid_argument` and `out_of_range` and
returns `carven::runtime::Outcome<std::int32_t, Failure>`.

The adapter is a C++ function template. Carven passes an `InvalidPort` value,
allowing C++ to deduce its generated type without spelling a compiler-private
namespace. `Result::success_from` constructs the successful value;
`Result::failure` constructs the declared failure. Parse the input before entering
the success factory so parse exceptions reach the adapter's handlers.

The `import(cpp)` declaration gives Carven the signature and failure contract.
Its `str` parameter uses `std::string_view`, and its failure parameter uses the
ordinary representation of a Carven structure. The `port` expression function
forwards the result with `?`. `report` handles `InvalidPort` using Carven's
`catch`.

Carven checks its calls and failure handling. C++ checks the native declaration,
template instantiation, and carrier compatibility. The adapter author decides
which native exceptions become which Carven failures. Other exceptions, such as
allocation failure, are not recovered here and terminate if they escape the
`noexcept` adapter. This target enables native C++ exceptions and RTTI
independently of the compiler's own build settings.

From the repository root:

```sh
./xmakew build
./xmakew build carven-example-native-parser
./xmakew run carven-example-native-parser
```

With an installed `carven`, run `carven main.cv` from this directory. Native
header paths are resolved from the working directory.

Expected output:

```text
Port: 8080
Invalid port
Invalid port
Invalid port
```

For direct header calls, see [Calling C++](../../docs/tutorial.md#calling-c).
