#pragma once

#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>
#include <system_error>
#include <type_traits>

namespace carven::number {

namespace detail {

template<typename Number>
auto parse(std::string_view text) noexcept -> std::optional<Number> {
    if (text.empty()) {
        return std::nullopt;
    }
    auto value = Number {};
    const auto result = [&]() noexcept {
        if constexpr (std::is_floating_point_v<Number>) {
            return std::from_chars(
                text.data(),
                text.data() + text.size(),
                value,
                std::chars_format::general
            );
        } else {
            return std::from_chars(text.data(), text.data() + text.size(), value);
        }
    }();
    if (result.ec != std::errc {} || result.ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

} // namespace detail

inline auto parse_i64(std::string_view text) noexcept -> std::optional<std::int64_t> {
    return detail::parse<std::int64_t>(text);
}

inline auto parse_u64(std::string_view text) noexcept -> std::optional<std::uint64_t> {
    return detail::parse<std::uint64_t>(text);
}

inline auto parse_f64(std::string_view text) noexcept -> std::optional<double> {
    return detail::parse<double>(text);
}

} // namespace carven::number
