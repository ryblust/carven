#pragma once

#include <cstdlib>

namespace carven::runtime {

[[noreturn]] inline auto unreachable() noexcept -> void {
#if defined(__clang__) || defined(__GNUC__)
    __builtin_unreachable();
#elif defined(_MSC_VER)
    __assume(false);
#else
    std::abort();
#endif
}

} // namespace carven::runtime
