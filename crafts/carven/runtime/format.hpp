#pragma once

#include "string.hpp"

#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <utility>

template<>
struct std::formatter<carven::runtime::String> final : std::formatter<std::string_view> {
    auto format(const carven::runtime::String& value, auto& context) const {
        return std::formatter<std::string_view>::format(value.as_str(), context);
    }
};

namespace carven::runtime {
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
using FormatArgument = decltype(carven::runtime::format_argument(std::declval<const T&>()));

// Cross-header String storage access stays private to proven formatting entries.
class StringFormatAccess final {
    static auto adopt(std::string&& bytes) noexcept -> String { return String(std::move(bytes)); }

    template<typename... Args>
    friend auto format_valid_utf8(
        std::format_string<FormatArgument<Args>...> format_string,
        const Args&... values
    ) noexcept -> String;
};

template<typename... Args>
auto format(
    std::format_string<FormatArgument<Args>...> format_string,
    const Args&... values
) noexcept -> String {
    return String::from_utf8(
        std::format(format_string, carven::runtime::format_argument(values)...)
    );
}

// Requires the completed std::format output to be valid UTF-8. Generated callers
// select this entry only with the preparation encoding proof; errors still terminate.
template<typename... Args>
auto format_valid_utf8(
    std::format_string<FormatArgument<Args>...> format_string,
    const Args&... values
) noexcept -> String {
    return StringFormatAccess::adopt(
        std::format(format_string, carven::runtime::format_argument(values)...)
    );
}

// Formatting inputs must not overlap destination storage or access destination
// through native aliases. Errors terminate; there is no rollback guarantee.
template<typename... Args>
auto append_format(
    String& destination,
    std::format_string<FormatArgument<Args>...> format_string,
    const Args&... values
) noexcept -> void {
    const auto formatted = carven::runtime::format(format_string, values...);
    destination.append(formatted.as_str());
}

// In addition to append_format's borrow contract, the completed output must be
// valid UTF-8. Format once, then append complete text without another validation scan.
template<typename... Args>
auto append_format_valid_utf8(
    String& destination,
    std::format_string<FormatArgument<Args>...> format_string,
    const Args&... values
) noexcept -> void {
    const auto formatted = carven::runtime::format_valid_utf8(format_string, values...);
    destination.append(formatted.as_str());
}

} // namespace carven::runtime
