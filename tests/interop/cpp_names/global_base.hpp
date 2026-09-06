#pragma once

#include <cstdint>

struct GlobalPoint final {
    std::int32_t x;
    std::int32_t y;
};

inline auto global_calculate(std::int32_t value) noexcept -> std::int32_t {
    return value * 2;
}
