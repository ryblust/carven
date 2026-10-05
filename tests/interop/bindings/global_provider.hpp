#pragma once

#include <vector>

// The importing module must include global_base.hpp before this header.
inline auto global_point() noexcept -> GlobalPoint {
    return {3, 4};
}

namespace global_native {

struct Value final {
    std::int32_t value;
};

inline auto distinct_object(const Value& converted, const Value& original) noexcept -> bool {
    return &converted != &original;
}

inline auto sum(const GlobalPoint& point) noexcept -> std::int32_t {
    return point.x + point.y;
}

} // namespace global_native
