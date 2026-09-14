#pragma once

#include <format>
#include <string_view>

struct FormatProbe final {
    bool invalid_utf8;
};

inline auto make_format_probe(bool invalid_utf8) noexcept -> FormatProbe {
    return {.invalid_utf8 = invalid_utf8};
}

template<>
struct std::formatter<FormatProbe> final : std::formatter<std::string_view> {
    auto format(const FormatProbe& value, std::format_context& context) const {
        if (!value.invalid_utf8) {
            throw std::format_error("provider failure");
        }
        return std::formatter<std::string_view>::format(std::string_view("\xff", 1), context);
    }
};
