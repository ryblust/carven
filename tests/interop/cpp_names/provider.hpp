#pragma once
#include <cstdint>
#include <utility>

namespace native {
inline std::int32_t trace = 0;
inline auto record(std::int32_t value) -> std::int32_t {
    trace = trace * 10 + value;
    return value;
}
inline auto observed() -> std::int32_t {
    return trace;
}
inline auto reset() -> void {
    trace = 0;
}
inline auto pair(std::int32_t left, std::int32_t right) -> std::int32_t {
    return left * 10 + right;
}
struct AccessProbe final {
    auto access() const noexcept -> std::int32_t { return 1; }
    auto access() noexcept -> std::int32_t { return 2; }
};
inline auto argument_access(const std::int32_t&) noexcept -> std::int32_t {
    return 1;
}
inline auto argument_access(std::int32_t& value) noexcept -> std::int32_t {
    ++value;
    return 2;
}
inline auto identity(const std::int32_t& value) -> const std::int32_t& {
    return value; // NOLINT(bugprone-return-const-ref-from-parameter)
}
struct Owner final {
    inline static std::int32_t live = 0;
    inline static std::int32_t copies = 0;
    inline static std::int32_t moves = 0;
    std::int32_t value;
    explicit Owner(std::int32_t number)
        : value(number) {
        ++live;
    }
    Owner(const Owner& other)
        : value(other.value) {
        ++copies;
        ++live;
    }
    Owner(Owner&& other)
        : value(std::exchange(other.value, 0)) {
        ++moves;
        ++live;
    }
    ~Owner() { --live; }
    auto read() const -> std::int32_t { return value; }
};
inline auto reset_owner_counts() noexcept -> void {
    Owner::copies = 0;
    Owner::moves = 0;
}
inline auto consume(Owner value) -> std::int32_t { // NOLINT(performance-unnecessary-value-param)
    return value.value;
}
inline auto copies() -> std::int32_t {
    return Owner::copies;
}
inline auto moves() -> std::int32_t {
    return Owner::moves;
}
inline auto live() -> std::int32_t {
    return Owner::live;
}
}

inline auto plain(std::int32_t value) -> std::int32_t {
    return value;
}
