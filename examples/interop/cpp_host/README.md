# Call Carven from a C++ application

Read `pricing.cv` first. Its `export(cpp)` function declares a scalar quantity
parameter and result, then computes a quantity discount. The build generates the
public header used by the C++ caller.

Then read `main.cpp`: it includes that header and `<iostream>`, defines the
process entry, and prints the result of calling the exported function. The build
links the generated implementation with this host.

The source module path determines the public namespace:
`carven::api::examples::interop::cpp_host::pricing`. The caller aliases it as
`pricing`. This public API exposes
fixed-width scalar parameters and results with a `noexcept` boundary.

From the repository root:

```sh
./xmakew build
./xmakew build carven-example-cpp-host
./xmakew run carven-example-cpp-host
```

Expected output:

```text
Price in cents:
1080
```

Change the quantity from 12 to 3 to use the regular unit price. The example's
caller supplies a small nonnegative quantity; this calculation does not validate
arbitrary inputs.
