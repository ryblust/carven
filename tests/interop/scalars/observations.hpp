#pragma once

#include <cstdint>

namespace observation_probe {

inline auto write(std::int32_t* value) noexcept -> std::int32_t {
    *value = 9;
    return 2;
}

inline auto take(std::int32_t&& value) noexcept -> std::int32_t {
    value = 9;
    return 2;
}

} // namespace observation_probe
