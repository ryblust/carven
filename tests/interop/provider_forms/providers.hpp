#pragma once

#include <cstddef>
#include <cstdint>

extern std::int32_t provider_forms_trace;

inline auto native_observe(std::size_t value) noexcept -> void {
    provider_forms_trace += static_cast<std::int32_t>(value);
}

inline auto header_twice(std::int32_t value) noexcept -> std::int32_t {
    return value * 2;
}

auto linked_offset(std::int32_t value) noexcept -> std::int32_t;
