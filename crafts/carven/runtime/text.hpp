#pragma once

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

inline auto str_bytes(std::string_view text) noexcept -> Slice<std::uint8_t> {
    return Slice<std::uint8_t>(
        std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size())
    );
}

// The borrowed range must contain valid UTF-8.
constexpr auto str_chars(std::string_view text) noexcept -> StrCharsView {
    return StrCharsView(text);
}


} // namespace carven::runtime
