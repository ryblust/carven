#pragma once

#include <format>
#include <string_view>

inline auto invalid_scalar() noexcept -> char32_t {
    return static_cast<char32_t>(0x110000);
}

struct InvalidText final {};

inline auto invalid_text() noexcept -> InvalidText {
    return {};
}

template<>
struct std::formatter<InvalidText> final : std::formatter<std::string_view> {
    auto format(const InvalidText&, std::format_context& context) const {
        auto output = context.out();
        *output++ = '\xff';
        return output;
    }
};
