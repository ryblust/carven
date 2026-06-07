# Carven Runtime Header

## Goal

Provide a small C++ header that contains the standard includes and helper
facilities required by generated Carven code.

This keeps generated C++ readable while avoiding scattered include decisions in
codegen.

## Proposed Shape

Generated C++ can include a stable runtime header:

```cpp
#include "carven/runtime.hpp"
```

The header should initially stay minimal:

- standard includes needed by generated code, such as `<array>`, `<cstdint>`,
  `<cstddef>`, `<type_traits>`, and `<utility>`
- helper traits used by generated pattern matching
- small utilities that encode Carven semantics but do not belong in user code

## Non-Goals

- Do not wrap or replace the C++ standard library.
- Do not become a large framework.
- Do not hide generated C++ behind opaque runtime APIs when direct C++ is clear.

## Future Uses

- Runtime-backed `match is` patterns for explicit type and value checks
- Range helpers for future `for i in 1..10`
- Slice/span helpers if Carven adds slice syntax
- Shared support for generated assertions, inline tests, or panic/trap behavior

## Open Questions

- Should the header be emitted next to generated C++ or installed as part of the
  Carven toolchain?
- Should generated code always include it, or only when a generated construct
  needs it?
- What namespace should runtime helpers use? Proposed default: `carven`.
