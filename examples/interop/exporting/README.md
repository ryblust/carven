# Call Carven from a C++ application

```sh
./xmakew build example-cpp-host
./xmakew run example-cpp-host
```

Run from the repository root after [building the compiler](../../README.md).

```text
Price in cents:
1080
```

`main.cpp` owns the entry point. `pricing.cv` exports a quantity discount
calculation using `export(cpp)`. The build generates the public header included
by the host and links the generated implementation with `main.cpp`.

The source module path determines the public namespace:
`carven::api::examples::interop::exporting::pricing`. Use this public API rather
than generated implementation namespaces. The function accepts and returns
fixed-width scalar values and is exposed as `noexcept`.

Change the quantity from 12 to 3 to use the regular unit price. The example's
caller supplies a small nonnegative quantity; this calculation does not validate
arbitrary inputs.
