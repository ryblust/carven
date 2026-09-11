#pragma once

#include "string.hpp"

#include <format>
#include <string_view>
#include <utility>

template<>
struct std::formatter<carven::runtime::String> final : std::formatter<std::string_view> {
    auto format(const carven::runtime::String& value, auto& context) const {
        return std::formatter<std::string_view>::format(value.as_str(), context);
    }
};

namespace carven::runtime {
namespace detail {

template<typename T>
auto format_argument(const T& value) noexcept -> const T& {
    // NOLINTNEXTLINE(bugprone-return-const-ref-from-parameter): Borrowed within the synchronous format call.
    return value;
}

inline auto format_argument(char32_t value) noexcept -> String {
    auto result = String();
    result.push(value);
    return result;
}

template<typename T>
using FormatArgument = decltype(format_argument(std::declval<const T&>()));

} // namespace detail

template<typename... Args>
auto format(
    std::format_string<detail::FormatArgument<Args>...> format_string,
    const Args&... values
) noexcept -> String {
    return String::from_utf8(std::format(format_string, detail::format_argument(values)...));
}

} // namespace carven::runtime
