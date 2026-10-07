# Carven standard crafts

Standard crafts own library APIs and their implementations. The current
[UTF craft](utf/README.md) provides encoding, validation, and text APIs.
The [SIMD craft](simd/README.md) provides byte and floating algorithms and bounded
block traversal over Carven's built-in vector primitives. Native SIMD primitives
use NEON, opt-in AVX2, or a portable fallback, with identical lane semantics in
constant execution.
The [JSON craft](json/README.md) validates, parses, edits, and writes owned values
using shared SIMD scans. The [number craft](number/README.md) converts complete
numeric text into scalar values.

Carven source uses language operations without importing runtime implementations.
The [compiler reference](../../../docs/compiler/README.md#craft-implementation-boundary)
defines how source operations reach native support.

Standard and user crafts share Carven's constant-execution and static-storage
facilities for admitted operations and results. Generic container integration is
[open work](../../../proposals/constant-storage.md#library-integration). The C++
standard library remains available through native interoperation.
