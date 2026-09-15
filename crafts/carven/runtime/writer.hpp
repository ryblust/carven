#pragma once

#include "string.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <exception>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

namespace carven::runtime {

// Synchronous writes into a borrowed String. Text must be valid UTF-8 and inputs
// must not overlap destination storage. Bounds describe the additional bytes
// written by this operation. Errors terminate without rollback.
class Writer final {
public:
    Writer(String& destination, std::size_t minimum_size, std::size_t maximum_size) noexcept
        : storage(destination.storage) {
        reserve(minimum_size, maximum_size);
    }

    // Carven observes dynamic text only after all source operands complete.
    // Bounds cover other fragments; supplied lengths are exact byte counts.
    Writer(
        String& destination,
        std::size_t minimum_size,
        std::size_t maximum_size,
        std::initializer_list<std::size_t> text_sizes
    ) noexcept
        : storage(destination.storage) {
        const auto available = storage.max_size() - storage.size();
        for (const auto size : text_sizes) {
            if (size > available || minimum_size > available - size) {
                std::terminate();
            }
            minimum_size += size;
            maximum_size = maximum_size > available - size ? available : maximum_size + size;
        }
        reserve(minimum_size, maximum_size);
    }

    auto append(std::string_view text) noexcept -> void { storage.append(text); }

    auto append(const String& text) noexcept -> void { append(text.as_str()); }

    auto boolean(bool value) noexcept -> void { append(value ? "true" : "false"); }

    auto character(char32_t value) noexcept -> void {
        const auto encoded = encode_valid_utf8(value);
        append(std::string_view(encoded.bytes.data(), encoded.width));
    }

    template<int Base, bool Uppercase, bool ZeroPad, typename Integer>
        requires (std::is_integral_v<Integer> && !std::is_same_v<Integer, bool>)
    auto integer(Integer value, std::size_t width) noexcept -> void {
        static_assert(Base == 2 || Base == 8 || Base == 10 || Base == 16);
        using Unsigned = std::make_unsigned_t<Integer>;
        auto magnitude = static_cast<Unsigned>(value);
        auto negative = false;
        if constexpr (std::is_signed_v<Integer>) {
            negative = value < 0;
            if (negative) {
                magnitude = Unsigned {0} - magnitude;
            }
        }
        std::array<char, std::numeric_limits<Unsigned>::digits> buffer;
        const auto converted =
            std::to_chars(buffer.data(), buffer.data() + buffer.size(), magnitude, Base);
        if (converted.ec != std::errc()) {
            std::terminate();
        }
        const auto digits = static_cast<std::size_t>(converted.ptr - buffer.data());
        if constexpr (Uppercase && Base == 16) {
            for (auto* cursor = buffer.data(); cursor != converted.ptr; ++cursor) {
                const auto byte = *cursor;
                *cursor = static_cast<char>(byte >= 'a' && byte <= 'f' ? byte - 'a' + 'A' : byte);
            }
        }
        const auto size = digits + static_cast<std::size_t>(negative);
        const auto padding = width > size ? width - size : 0;
        if constexpr (!ZeroPad) {
            storage.append(padding, ' ');
        }
        if (negative) {
            storage.push_back('-');
        }
        if constexpr (ZeroPad) {
            storage.append(padding, '0');
        }
        storage.append(buffer.data(), digits);
    }

private:
    auto reserve(std::size_t minimum_size, std::size_t maximum_size) noexcept -> void {
        const auto available = storage.max_size() - storage.size();
        if (minimum_size > available) {
            std::terminate();
        }
        // Reserve the bound only when growth is certain. A large type bound
        // alone must not force short results out of existing storage.
        if (minimum_size > storage.capacity() - storage.size()) {
            const auto capacity = maximum_size < available ? maximum_size : available;
            storage.reserve(storage.size() + capacity);
        }
    }

    std::string& storage;
};

} // namespace carven::runtime
