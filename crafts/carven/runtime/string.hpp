#pragma once

#include <carven/runtime/text.hpp>

#include <array>
#include <string>
#include <utility>

namespace carven::runtime {

// Views borrow current storage; the owner must remain alive and unchanged for each borrow.
// from_str and append require valid UTF-8; push requires a Unicode scalar.
class String final {
public:
    constexpr String() noexcept = default;
    constexpr String(const String&) noexcept = default;
    constexpr String(String&&) noexcept = default;
    constexpr auto operator=(const String&) noexcept -> String& = default;
    constexpr auto operator=(String&&) noexcept -> String& = default;
    constexpr ~String() = default;

    static constexpr auto from_str(std::string_view text) noexcept -> String {
        auto result = String();
        result.append(text);
        return result;
    }

    // Native producers cross the UTF-8 boundary before transferring their storage.
    static constexpr auto from_utf8(std::string bytes) noexcept -> String {
        checked_utf8(bytes);
        auto result = String();
        result.storage = std::move(bytes);
        return result;
    }

    constexpr auto size() const noexcept -> std::size_t { return storage.size(); }

    constexpr auto empty() const noexcept -> bool { return storage.empty(); }

    constexpr auto as_str() const noexcept -> std::string_view { return storage; }

    constexpr auto append(std::string_view text) noexcept -> void { storage.append(text); }

    constexpr auto push(char32_t scalar) noexcept -> void {
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
        append(std::string_view(bytes.data(), width));
    }

    constexpr auto clear() noexcept -> void { storage.clear(); }

    constexpr auto operator==(const String& other) const noexcept -> bool {
        return storage == other.storage;
    }

private:
    std::string storage;
};

} // namespace carven::runtime
