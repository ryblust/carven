#pragma once

#include <cstdint>
#include <memory>

namespace pointer_values {
inline std::int32_t first = 10;
inline std::int32_t second = 20;

inline auto get() noexcept -> std::int32_t* {
    return &first;
}

inline auto other() noexcept -> std::int32_t* {
    return &second;
}

template<typename T>
auto address(T& value) noexcept -> T* {
    return std::addressof(value);
}
}
