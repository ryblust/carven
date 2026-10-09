#pragma once

#include <cstdint>

inline auto configured_value() noexcept -> std::int32_t {
    return CARVEN_FILE_VALUE;
}

auto native_offset() noexcept -> std::int32_t;
