#pragma once

#include <vector>

// The importing module must include global_base.hpp before this header.
inline auto global_point() noexcept -> GlobalPoint {
    return {3, 4};
}

namespace global_native {
inline auto sum(const GlobalPoint& point) noexcept -> std::int32_t {
    return point.x + point.y;
}
}
