#pragma once

#include "text.hpp"

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
        const auto encoded = encode_valid_utf8(scalar);
        append(std::string_view(encoded.bytes.data(), encoded.width));
    }

    constexpr auto clear() noexcept -> void { storage.clear(); }

    constexpr auto operator==(const String& other) const noexcept -> bool {
        return storage == other.storage;
    }

private:
    std::string storage;
};

} // namespace carven::runtime
