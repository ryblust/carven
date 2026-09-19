# Call Carven from a C++ application

In `pricing.cv`, `price_cents` computes a quantity discount, and `quote`
returns an owning `String`. `append_note` uses Write to modify an existing String;
`finish` uses Take to receive and return ownership.

The C++ host in `main.cpp` includes the generated API header, creates a quote,
passes it by mutable reference to `append_note`, and transfers it into `finish`
with `std::move`. The returned owner keeps the bytes alive while `as_str()` is
printed.

The source module path determines the public namespace:
`carven::api::examples::cpp_host::pricing`. The caller aliases it as `pricing`.
The API header includes the required runtime definitions. Its functions use the ordinary C++ representations
of Carven types, with `noexcept` boundaries:

- `String` results are owning `carven::runtime::String` values.
- `str` parameters are `std::string_view` values.
- Write String parameters are mutable references.
- Take String parameters are owned values.

Use generated headers and runtime support from the same compiler revision.
The build
links the generated implementation with the native host that defines `main`.

From the repository root:

```sh
./xmakew build
./xmakew build carven-example-cpp-host
./xmakew run carven-example-cpp-host
```

Expected output:

```text
12 items: 1080 cents (delivery included)
```

The caller supplies a nonnegative quantity; the calculation assumes valid input. For native exceptions converted into declared Carven failures,
see the [parser adapter](../call_cpp/README.md).
