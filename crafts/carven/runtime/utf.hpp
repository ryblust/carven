#pragma once

#include "simd/simd.hpp"
#include "trap.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <type_traits>

namespace carven::runtime {

namespace detail {

constexpr auto utf8_scalar_is_valid(std::string_view text) noexcept -> bool {
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

// Each block compares continuation bytes with the lead bytes that require them.
// The preceding block supplies up to three bytes of context. Special second-byte
// bounds exclude overlong encodings, surrogates, and values above U+10FFFF.
inline auto utf8_blocks_are_valid(std::string_view text) noexcept -> bool {
    using Bytes = simd::U8x32;
    const auto byte = [](std::uint8_t value) noexcept {
        return Bytes::splat(value);
    };
    auto previous = byte(0);
    auto offset = std::size_t {0};
    while (text.size() - offset >= 32) {
        auto storage = std::array<std::uint8_t, 32>();
        // The loop proves this complete object-representation copy readable.
        // No pointer cast, padding read, or reporting operation is needed.
        std::memcpy(storage.data(), text.data() + offset, storage.size());
        const auto current = Bytes::from_array(storage);
        if (((previous | current) >= byte(0x80)).any()) {
            const auto prev1 = previous.extract<31>(current);
            const auto prev2 = previous.extract<30>(current);
            const auto prev3 = previous.extract<29>(current);
            const auto continuation = (current & byte(0xc0)) == byte(0x80);
            const auto required =
                (prev1 >= byte(0xc0)) | (prev2 >= byte(0xe0)) | (prev3 >= byte(0xf0));
            const auto invalid_lead = (current >= byte(0xc0)) & (current < byte(0xc2));
            const auto invalid_second = ((prev1 == byte(0xe0)) & (current < byte(0xa0)))
                | ((prev1 == byte(0xed)) & (current >= byte(0xa0)))
                | ((prev1 == byte(0xf0)) & (current < byte(0x90)))
                | ((prev1 == byte(0xf4)) & (current >= byte(0x90)));
            if (((continuation ^ required) | invalid_lead | (current >= byte(0xf5))
                 | invalid_second)
                    .any()) {
                return false;
            }
        }
        previous = current;
        offset += 32;
    }
    // Revisit at most one scalar so a sequence split at the last block boundary
    // is checked together with its tail, including a truncated final full block.
    if (offset != 0 && static_cast<unsigned char>(text[offset - 1]) >= 0x80) {
        --offset;
        while ((static_cast<unsigned char>(text[offset]) & 0xc0u) == 0x80u) {
            --offset;
        }
    }
    return utf8_scalar_is_valid(text.substr(offset));
}

} // namespace detail

constexpr auto utf8_is_valid(std::string_view text) noexcept -> bool {
    if (!std::is_constant_evaluated() && simd::hardware_accelerated && text.size() >= 32) {
        return detail::utf8_blocks_are_valid(text);
    }
    return detail::utf8_scalar_is_valid(text);
}

// Validates ingress without replacing borrowed storage or extending its lifetime.
// Invalid input terminates.
constexpr auto checked_utf8(std::string_view text, SourceSite site) noexcept -> std::string_view {
    if (!utf8_is_valid(text)) {
        trap("text is not valid UTF-8", site);
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
constexpr auto checked_unicode_scalar(char32_t value, SourceSite site) noexcept -> char32_t {
    if (value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) {
        trap("value is not a Unicode scalar value", site);
    }
    return value;
}

} // namespace carven::runtime
