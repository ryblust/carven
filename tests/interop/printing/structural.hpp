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

inline auto display_bool() noexcept -> bool {
    return true;
}

inline auto display_double() noexcept -> double {
    return 1.5;
}

inline auto display_code_unit() noexcept -> char16_t {
    return u'A';
}

inline auto display_function() noexcept {
    return &display_bool;
}

inline auto display_null_cstring() noexcept -> const char* {
    return nullptr;
}
