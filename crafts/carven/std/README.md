# Carven standard crafts

Standard crafts own library APIs and their implementations.

- [IO](io/README.md) provides asynchronous standard-input bytes and checked text
  through `std::io`, using the ordinary operation and failure contracts.
- [UTF](utf/README.md) provides encoding, validation, and text APIs.
- [SIMD](simd/README.md) provides byte and floating algorithms and bounded
block traversal over Carven's built-in vector primitives. Native primitives use
NEON, opt-in AVX2, or a portable fallback, with identical lane semantics in
constant execution.
- [Time](time/README.md) provides checked time intervals, monotonic
time points, and cancellable waiting through `std::time.duration`,
`std::time.clock`, and `std::time.sleep`. `std::time.timeout` defines the
nominal deadline failure.
- [JSON](json/README.md) validates, parses, edits, and writes owned values
using shared SIMD scans.
- [Number](number/README.md) converts complete numeric text into scalar values.

Import specialized primitives from the compiler-provided `std::async`, `std::simd`,
and `std::time` modules. Common core, printing, and test names are available by
default. Source APIs use the paths and declaration visibility of their `.cv`
modules.

Carven source uses language operations without importing runtime implementations.
Crafts own their native support alongside their source modules. Runtime support
provides shared language operations and does not depend on standard crafts.

Use angle brackets and the complete `carven/` path for native imports and
includes, such as `import <carven/std/time/native.hpp>;` and
`#include <carven/runtime/execution/execution.hpp>`. Add the installed or project
Crafts root to the native include search directories.

Standard and user crafts share type checking, ownership, and static execution
rules. The C++ standard library is available through native interoperation.
