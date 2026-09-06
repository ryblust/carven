# Adapt a C++ parser

```sh
./xmakew build example-native-parser
./xmakew run example-native-parser
```

Run from the repository root after [building the compiler](../../README.md).

```text
Port:
8080
Invalid port
Invalid port
Invalid port
```

Read `main.cv` alongside `parser.hpp`. The provider declares `parse_port` in
the global C++ namespace. `import "parser.hpp";` includes its declarations;
`::parse_port(text)` directly names the global function without introducing a
Carven short name. Carven does not parse the header. C++ checks the referenced
declaration and call.

The other import, `using example::print`, introduces the short name `print`
from a named namespace. To give the parser a short name too, use
`import "parser.hpp" using parse_port;` and call `parse_port(text)`. Multiple
global declarations can use a list such as `using { Point, calculate }`.

Carven text crosses the parser call as `std::string_view`; the adapter
constructs a `std::string` for `std::stoi`.

The C++ adapter catches `invalid_argument` and `out_of_range`, validates complete
consumption and the port range, and returns -1 for invalid input. It retains
`std::stoi`'s acceptance of leading whitespace and a plus sign. The Carven
wrapper explicitly converts the external numeric result to i32 and turns -1
into its own `InvalidPort` failure. Carven's `catch` handles that value; it does
not catch a C++ exception.

The provider has no `noexcept` declaration. Its expected parse exceptions are
handled internally. Other exceptions, such as allocation failure, are not
recovered here and terminate if they escape the generated `noexcept` boundary.
This target enables native C++ exceptions and RTTI independently of the
compiler's own build settings.

Try `"65536"`, `"0"`, or `"443"`. The adapter could also be implemented in a
linked C++ source or a top-level `#[cpp]` fragment; a header keeps both sides
visible in this example.

Next: [C++ host](../exporting/).
