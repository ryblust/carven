#pragma once

#include <cstdint>

inline auto discarded_invalid_scalar() noexcept -> char32_t {
    return static_cast<char32_t>(0x110000);
}

namespace discarded_probe {

inline auto calls = 0;
inline auto owners = 0;

struct [[nodiscard]] Result final {
    Result() noexcept { ++owners; }

    Result(const Result&) = delete;
    Result(Result&&) = delete;

    ~Result() { --owners; }
};

[[nodiscard]] inline auto scalar() noexcept -> std::int32_t {
    return ++calls;
}

inline auto result() noexcept -> Result {
    ++calls;
    return {};
}

inline auto reset() noexcept -> void {
    calls = 0;
}

inline auto call_count() noexcept -> std::int32_t {
    return calls;
}

inline auto owner_count() noexcept -> std::int32_t {
    return owners;
}

} // namespace discarded_probe
