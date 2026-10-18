#pragma once
#include <cstdio>

namespace stream_macros {

inline auto input() noexcept -> std::FILE* {
    return stdin;
}

} // namespace stream_macros

// Exercise an expression-valued CRT macro on every host. Its original stream
// remains available through input(), and the macro stays defined for consumers.
#undef stdin
#define stdin (::stream_macros::input())
