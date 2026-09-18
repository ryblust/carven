#pragma once

#include <format>

struct DisplayNative final {
    DisplayNative() = default;
    DisplayNative(const DisplayNative&) = delete;
    DisplayNative(DisplayNative&&) = default;
    auto operator=(const DisplayNative&) -> DisplayNative& = delete;
};

struct DisplayCopyable final {};

template<>
struct std::formatter<DisplayNative> : std::formatter<std::string_view> {
    auto format(const DisplayNative&, std::format_context& context) const
        -> std::format_context::iterator {
        return std::formatter<std::string_view>::format("custom", context);
    }
};
