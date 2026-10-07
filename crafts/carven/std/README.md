# Carven standard crafts

Standard crafts own library APIs and their implementations.
The [`std::async` module](../../../docs/language/async.md) selects compiler-defined
cancellation and yield operations for async source programs. The current
[UTF craft](utf/README.md) provides encoding, validation, and text APIs.
The [SIMD craft](simd/README.md) provides byte and floating algorithms and bounded
block traversal over Carven's built-in vector primitives. Native primitives use
NEON, opt-in AVX2, or a portable fallback, with identical lane semantics in
constant execution.

Carven source uses language operations without importing runtime implementations.
The [compiler reference](../../../docs/compiler/README.md#craft-implementation-boundary)
defines how source operations reach native support.

Standard and user crafts share Carven's constant-execution and static-storage
facilities for admitted operations and results. Generic container integration is
[open work](../../../proposals/constant-storage.md#library-integration). The C++
standard library remains available through native interoperation.
