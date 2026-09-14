#pragma once

#include "utf.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace carven::runtime {

// The formatting header defines the private bridge for formatting storage access.
class StringFormatAccess;
class Writer;

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
        return String(std::move(bytes));
    }

    constexpr auto size() const noexcept -> std::size_t { return storage.size(); }

    constexpr auto empty() const noexcept -> bool { return storage.empty(); }

    constexpr auto as_str() const noexcept -> std::string_view { return storage; }

    constexpr auto append(std::string_view text) noexcept -> void { storage.append(text); }

    constexpr auto clear() noexcept -> void { storage.clear(); }

    constexpr auto push(char32_t scalar) noexcept -> void {
        const auto encoded = encode_valid_utf8(scalar);
        append(std::string_view(encoded.bytes.data(), encoded.width));
    }

    constexpr auto operator==(const String& other) const noexcept -> bool {
        return storage == other.storage;
    }

private:
    // Only validated ingress and proven formatting may adopt native storage.
    explicit constexpr String(std::string&& bytes) noexcept
        : storage(std::move(bytes)) {}

    std::string storage;

    friend class StringFormatAccess;
    friend class Writer;
};

} // namespace carven::runtime
