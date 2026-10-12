#pragma once

#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>
#include <system_error>
#include <type_traits>

namespace carven::number {

template<typename Number>
struct Parser final {
    static_assert(
        (std::is_integral_v<Number>
         && !std::is_same_v<Number, bool>
         && !std::is_same_v<Number, char32_t>)
            || std::is_same_v<Number, float>
            || std::is_same_v<Number, double>,
        "number parsing requires a Carven integer or floating-point type"
    );

    static auto parse(std::string_view text) noexcept -> std::optional<Number> {
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
};

} // namespace carven::number
