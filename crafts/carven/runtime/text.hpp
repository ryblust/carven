#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ranges>
#include <span>
#include <string_view>

namespace carven::runtime {

namespace detail {

struct ByteProjection final {
    constexpr auto operator()(char value) const noexcept -> std::uint8_t {
        return static_cast<std::uint8_t>(static_cast<unsigned char>(value));
    }
};

} // namespace detail

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

using StrBytesView =
    decltype(std::views::transform(std::string_view {}, detail::ByteProjection {}));

class StrCharsView final {
public:
    class Iterator final {
    private:
        struct DecodedUTF8 final {
            char32_t scalar;
            std::size_t width;
        };

        static constexpr auto decode_valid_utf8(std::span<const char> nonempty_bytes) noexcept
            -> DecodedUTF8 {
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

    public:
        constexpr Iterator(const char* current, const char* end) noexcept
            : current(current),
              end_pointer(end) {
            refresh();
        }

        constexpr auto operator*() const noexcept -> char32_t { return decoded.scalar; }

        constexpr auto operator++() noexcept -> Iterator& {
            current += decoded.width;
            refresh();
            return *this;
        }

        constexpr auto operator!=(const Iterator& other) const noexcept -> bool {
            return current != other.current;
        }

    private:
        constexpr auto refresh() noexcept -> void {
            if (current != end_pointer) {
                decoded = decode_valid_utf8(
                    std::span<const char>(current, static_cast<std::size_t>(end_pointer - current))
                );
            }
        }

        const char* current;
        const char* end_pointer;
        DecodedUTF8 decoded {.scalar = 0, .width = 0};
    };

    constexpr explicit StrCharsView(std::string_view text) noexcept
        : text(text) {}

    constexpr auto begin() const noexcept -> Iterator {
        return Iterator(text.data(), text.data() + text.size());
    }

    constexpr auto end() const noexcept -> Iterator {
        return Iterator(text.data() + text.size(), text.data() + text.size());
    }

private:
    std::string_view text;
};

constexpr auto str_bytes(std::string_view text) noexcept -> StrBytesView {
    return std::views::transform(text, detail::ByteProjection {});
}

// The borrowed range must contain valid UTF-8.
constexpr auto str_chars(std::string_view text) noexcept -> StrCharsView {
    return StrCharsView(text);
}

// Validates an external value before it becomes a Carven char.
constexpr auto checked_unicode_scalar(char32_t value) noexcept -> char32_t {
    if (value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) {
        std::abort();
    }
    return value;
}

} // namespace carven::runtime
