# Deduced C++ values

Run from the repository root:

```sh
./xmakew build
./xmakew run carven examples/native_values/main.cv
```

Expected output:

```text
Snapshot: 1, count: 4
Current first: 9
```

`vector { 1, 2, 3 }` delegates braced construction and class template argument
deduction to C++. Carven retains the resulting expression type for subsequent
indexing, member calls, and storage. It does not resolve the C++ template or
inspect its implementation. Explicit `vector<i32>` construction is also supported.

Native indexing uses the provider's `operator[]` and bounds contract. Carven
operator declarations are not needed. `let first = values[0]` creates an owned
snapshot even if the native operator returns a reference. `var` permits mutable
receiver operations; a `let` receiver supplies const access.

Ordinary constructor and call arguments supply Read access (`const T&`), `&value`
supplies Write (`T&`), and `&&value` supplies Take (`T&&`). Those categories also
participate in deduction guides and overload resolution. Braced arguments are
evaluated once in source order. Native validity and overload diagnostics belong
to C++; Carven checks its own access, availability, and evaluation contracts.

The example uses interpolation to compose labels and values. Direct printing such
as `println(values[0])` also displays the native numeric result. Other external
types without a supported scalar representation display `<opaque>`; structural
display does not call user formatters. Native alias retention and iterator
invalidation follow the provider's contract.
