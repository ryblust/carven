#pragma once

#include "slice.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string_view>

namespace carven::runtime {

constexpr auto utf8_is_valid(std::string_view text) noexcept -> bool {
    auto offset = std::size_t {0};
    while (offset < text.size()) {
        const auto first = static_cast<unsigned char>(text[offset]);
        if (first < 0x80) {
            ++offset;
            continue;
        }
        const auto continuation = [&](std::size_t index) constexpr noexcept {
            return index < text.size()
                && (static_cast<unsigned char>(text[index]) & 0xc0u) == 0x80u;
        };
        if (first >= 0xc2 && first <= 0xdf) {
            if (!continuation(offset + 1)) {
                return false;
            }
            offset += 2;
            continue;
        }
        if (first >= 0xe0 && first <= 0xef) {
            if (!continuation(offset + 1) || !continuation(offset + 2)) {
                return false;
            }
            const auto second = static_cast<unsigned char>(text[offset + 1]);
            if ((first == 0xe0 && second < 0xa0) || (first == 0xed && second >= 0xa0)) {
                return false;
            }
            offset += 3;
            continue;
        }
        if (first >= 0xf0 && first <= 0xf4) {
            if (!continuation(offset + 1)
                || !continuation(offset + 2)
                || !continuation(offset + 3)) {
                return false;
            }
            const auto second = static_cast<unsigned char>(text[offset + 1]);
            if ((first == 0xf0 && second < 0x90) || (first == 0xf4 && second >= 0x90)) {
                return false;
            }
            offset += 4;
            continue;
        }
        return false;
    }
    return true;
}

// Validates ingress without copying bytes or extending the source lifetime.
// Invalid input terminates.
constexpr auto checked_utf8(std::string_view text) noexcept -> std::string_view {
    if (!utf8_is_valid(text)) {
        std::abort();
    }
    return text;
}

struct DecodedUTF8 final {
    char32_t scalar;
    std::size_t width;
};

// Requires nonempty, valid UTF-8 input.
constexpr auto decode_valid_utf8(std::span<const char> nonempty_bytes) noexcept -> DecodedUTF8 {
    const auto first = static_cast<unsigned char>(nonempty_bytes.front());
    if (first < 0x80) {
        return {.scalar = static_cast<char32_t>(first), .width = 1};
    }
    const auto second = static_cast<unsigned char>(nonempty_bytes[1]);
    if (first <= 0xdf) {
        return {
            .scalar = static_cast<char32_t>(((first & 0x1fu) << 6) | (second & 0x3fu)),
            .width = 2,
        };
    }
    const auto third = static_cast<unsigned char>(nonempty_bytes[2]);
    if (first <= 0xef) {
        return {
            .scalar = static_cast<char32_t>(
                ((first & 0x0fu) << 12) | ((second & 0x3fu) << 6) | (third & 0x3fu)
            ),
            .width = 3,
        };
    }
    const auto fourth = static_cast<unsigned char>(nonempty_bytes[3]);
    return {
        .scalar = static_cast<char32_t>(
            ((first & 0x07u) << 18) | ((second & 0x3fu) << 12) | ((third & 0x3fu) << 6)
            | (fourth & 0x3fu)
        ),
        .width = 4,
    };
}

struct EncodedUTF8 final {
    std::array<char, 4> bytes;
    std::size_t width;
};

// Requires a Unicode scalar value.
constexpr auto encode_valid_utf8(char32_t scalar) noexcept -> EncodedUTF8 {
    auto bytes = std::array<char, 4>();
    auto width = std::size_t {0};
    if (scalar < 0x80) {
        bytes[0] = static_cast<char>(scalar);
        width = 1;
    } else if (scalar < 0x800) {
        bytes[0] = static_cast<char>(0xc0u | (scalar >> 6));
        bytes[1] = static_cast<char>(0x80u | (scalar & 0x3fu));
        width = 2;
    } else if (scalar < 0x10000) {
        bytes[0] = static_cast<char>(0xe0u | (scalar >> 12));
        bytes[1] = static_cast<char>(0x80u | ((scalar >> 6) & 0x3fu));
        bytes[2] = static_cast<char>(0x80u | (scalar & 0x3fu));
        width = 3;
    } else {
        bytes[0] = static_cast<char>(0xf0u | (scalar >> 18));
        bytes[1] = static_cast<char>(0x80u | ((scalar >> 12) & 0x3fu));
        bytes[2] = static_cast<char>(0x80u | ((scalar >> 6) & 0x3fu));
        bytes[3] = static_cast<char>(0x80u | (scalar & 0x3fu));
        width = 4;
    }
    return {bytes, width};
}

// Validates an external value before it becomes a Carven char.
constexpr auto checked_unicode_scalar(char32_t value) noexcept -> char32_t {
    if (value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) {
        std::abort();
    }
    return value;
}

// Requires valid UTF-8. The returned text borrows the input storage.
inline auto utf8_text(Slice<std::uint8_t> bytes) noexcept -> std::string_view {
    return bytes.empty()
        ? std::string_view {}
        : std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

} // namespace carven::runtime
