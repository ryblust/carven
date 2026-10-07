#pragma once

#include <carven/runtime/void.hpp>

#include <cstdint>

inline auto accept_void_value(carven::runtime::Void) noexcept -> std::int32_t {
    return 17;
}
