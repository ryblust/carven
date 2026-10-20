# Development

These references guide changes to Carven's language, compiler, runtime, and
repository tests.

## References

| Task | Reference |
| --- | --- |
| Organize project source and write Carven or C++ | [Conventions](conventions.md): layout, imports, modules, naming, values, and failure handling |
| Place tests and validate a change | [Testing](testing.md): suite responsibilities, fixtures, assertions, builds, and static analysis |
| Evaluate a language or implementation design | [Principles](principles.md): semantic authority, library ownership, native boundaries, safety, performance, and feature admission |

Repository formatting and module-build support live in `xmake/`; benchmark
workloads and commands live in `benchmarks/`.
