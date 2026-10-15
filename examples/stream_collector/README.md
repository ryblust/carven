# Stream collector

A bidirectional C++ interoperation example that merges two memory streams,
removes duplicate records, and returns a structured report.

1. [`main.cpp`](main.cpp) creates two native input streams and calls Carven's exported `collect`.
2. [`workflow.cv`](workflow.cv) calls the imported C++ `next_record`, keeps unique records, and counts the input and duplicates.
3. C++ receives the Carven `Report`, reads its fields, and prints the records and statistics.

The first stream contains `alpha`, `beta`, `alpha`, and `gamma`. The second
contains `beta`, `delta`, `gamma`, and `delta`. Carven drains the first stream,
then the second, preserving the order in which each unique record first appears.
Deduplication compares the full text, including case and whitespace.

[`stream.hpp`](stream.hpp) declares the native stream holder and provider function.
[`stream.cpp`](stream.cpp) implements the provider with `std::getline`, skipping
empty lines. Its borrowed result remains valid until the next read; Carven
copies each unique record into owning String storage before reading again.
`Report.records` owns those Strings, `total` counts all nonempty input records,
and `duplicates` counts repeated records within or across the streams. The unique
count is `records.size()` in C++. The report remains valid after the streams
are destroyed.

```sh
./xmakew run carven-example-stream-collector
```

Output:

```text
Read 8 records; unique: 4; duplicates: 4
alpha
beta
gamma
delta
```

The native declaration uses `std::string_view`; the Carven contract uses `str`.
The generated API header supplies the complete `Report` representation, including
its `Vector<String>`. C++ uses `auto` for the returned report, accesses its fields,
and iterates its records directly. The native build compiles the two C++ source
files alongside the generated Carven implementation.
