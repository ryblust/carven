#pragma once

#include <cstdint>

namespace namespaced_provider {

struct Value final {
    std::int32_t number;
};

inline auto invoke(const Value& value) noexcept -> std::int32_t {
    return value.number + 1;
}

inline auto invoke(std::int32_t value) noexcept -> std::int32_t {
    return value + 2;
}

inline auto global_target(const Value& value) noexcept -> std::int32_t {
    return value.number + 3;
}

inline auto ordinary(const Value&) noexcept -> std::int32_t {
    return -1;
}

inline auto async(std::int32_t value) noexcept -> std::int32_t {
    return value + 4;
}

template<typename T>
inline auto identity(T value) noexcept -> T {
    return value;
}

struct Operations final {
    static auto value() noexcept -> std::int32_t { return 42; }
};

} // namespace namespaced_provider

using namespaced_provider::global_target;
