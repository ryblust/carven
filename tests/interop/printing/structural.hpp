#pragma once

#include <format>
#include <string>

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

inline auto display_cstring() noexcept -> const char* {
    return "native\ntext";
}

inline auto display_mutable_cstring() noexcept -> char* {
    static char text[] = "mutable";
    return text;
}

inline auto display_long_cstring() noexcept -> const char* {
    static const auto text = std::string(17000, 'x');
    return text.c_str();
}
