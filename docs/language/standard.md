# Standard capabilities

[Language](README.md)

This page indexes standard types, operations, and library modules. Each linked
reference defines its API's signatures, access, lifetimes, failures, and execution
capabilities.

## Modules and API references

Standard APIs use `std::` paths with ordinary import selections and aliases.
[Modules and names](modules.md#standard-modules-and-defaults) defines default
bindings and lookup. Associated functions use the selected type, such as
`i64::parse(text)`, `str::from_utf8(bytes)`, and `f32x8::splat(0.0)`.
Value methods use their receiver, such as `samples.sqrt()`.

| Domain | Public entries | Capability and API reference |
| --- | --- | --- |
| Core | `std::core` | [Numeric types, parsing, and floating methods](types.md), [Unicode text and formatting](text.md), [owning vectors](aggregates.md#owning-vectors), [ranges](control-flow.md#control-flow-and-loops), [slices](types.md#read-only-slices), and [pointer access](pointers.md) |
| Input and output | `std::io` | [Printing, standard input, and byte framing](io.md) |
| Async | `std::async`; async syntax | [Cold operations, composition, and lexical children](async.md) |
| Time | `std::time`; duration literals | [Intervals, monotonic time, waits, and deadlines](time.md) |
| UTF | `std::utf` | [Errors, encoding, validation, and text processing](utf.md) |
| SIMD | `std::simd`, `std::simd.bytes`, `std::simd.floats`, `std::simd.scan` | [Vector and mask primitives, block traversal, and byte-set queries](simd.md) |
| JSON | `std::json` | [Validation, owned values, parsing, and writing](json.md) |
| Networking | `std::net` | [TCP reads and writes over host-owned connections](net.md) |
| Testing | `std::testing`; `assert` from `std::core` | [Assertions and test reports](execution.md#tests) |

Import standard types that are not available by default when naming them in
source; importing a function does not also bind the types in its signature.
APIs marked `const fn` support explicit static execution.
