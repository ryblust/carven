#pragma once

#include <carven/runtime/diagnostics/report.hpp>

#include <bit>
#include <concepts>
#include <cstdint>
#include <optional>
#include <string_view>

namespace carven::runtime {

constexpr auto check_float_clamp_bounds(bool ordered, SourceSite site) noexcept -> void {
    if (!ordered) {
        assertion_failed(site, "assert", std::string_view("lower <= upper"), std::nullopt, "");
    }
}

constexpr auto float_abs(float value) noexcept -> float {
    return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) & 0x7fffffffu);
}

constexpr auto float_abs(double value) noexcept -> double {
    return std::bit_cast<double>(std::bit_cast<std::uint64_t>(value) & 0x7fffffffffffffffull);
}

template<std::floating_point Type>
constexpr auto float_min(Type first, Type second) noexcept -> Type {
    if (first != first) {
        return first;
    }
    if (second != second) {
        return second;
    }
    return second < first ? second : first;
}

template<std::floating_point Type>
constexpr auto float_max(Type first, Type second) noexcept -> Type {
    if (first != first) {
        return first;
    }
    if (second != second) {
        return second;
    }
    return second > first ? second : first;
}

template<std::floating_point Type>
constexpr auto float_clamp(Type value, Type lower, Type upper, SourceSite site) noexcept -> Type {
    check_float_clamp_bounds(lower <= upper, site);
    return value < lower ? lower : value > upper ? upper : value;
}

} // namespace carven::runtime
