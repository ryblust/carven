# Carven standard crafts

Standard crafts own library APIs and their implementations. The current
[UTF craft](utf/README.md) provides encoding, validation, and text APIs.

Carven source uses language operations without importing runtime implementations.
The [compiler reference](../../../docs/compiler.md#craft-implementation-boundary)
defines how source operations reach native support.

Standard and user crafts share Carven's constant-execution and static-storage
facilities for admitted operations and results. Generic container integration is
[open work](../../../proposals/constant-storage.md#library-integration). The C++
standard library remains available through native interoperation.
