#pragma once

#include "slice.hpp"
#include "string.hpp"
#include "utf.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace carven::runtime {

class StrCharsView final {
public:
    class Iterator final {
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

inline auto text_bytes(std::string_view text) noexcept -> Slice<std::uint8_t> {
    return Slice<std::uint8_t>(
        std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size())
    );
}

// The borrowed range must contain valid UTF-8.
constexpr auto text_chars(std::string_view text) noexcept -> StrCharsView {
    return StrCharsView(text);
}

// Views borrow the String's current storage; its owner must remain alive and unchanged.
inline auto text_bytes(const String& text) noexcept -> Slice<std::uint8_t> {
    return text_bytes(text.as_str());
}

constexpr auto text_chars(const String& text) noexcept -> StrCharsView {
    return text_chars(text.as_str());
}

// Requires valid UTF-8. The returned text borrows the input storage.
inline auto utf8_text(Slice<std::uint8_t> bytes) noexcept -> std::string_view {
    return bytes.empty()
        ? std::string_view {}
        : std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

} // namespace carven::runtime
