#pragma once

#include "utf8.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace carven::runtime {

// The formatting header defines the private bridge for formatting storage access.
class StringFormatAccess;
class FormatWriter;

// Views borrow current storage; the owner must remain alive and unchanged for each borrow.
// from_str and append require valid UTF-8; push requires a Unicode scalar.
class String final {
public:
    constexpr String() noexcept = default;

    constexpr String(const String& other) noexcept
        : storage(std::in_place_type<std::string>, other.as_str()) {}

    constexpr String(String&&) noexcept = default;

    constexpr auto operator=(const String& other) noexcept -> String& {
        if (this != &other) {
            storage.emplace<std::string>(other.as_str());
        }
        return *this;
    }

    constexpr auto operator=(String&&) noexcept -> String& = default;
    constexpr ~String() = default;

    static constexpr auto from_str(std::string_view text) noexcept -> String {
        auto result = String();
        result.append(text);
        return result;
    }

    static constexpr auto from_static(std::string_view text) noexcept -> String {
        return String(text);
    }

    // Native producers cross the UTF-8 boundary before transferring their storage.
    static constexpr auto from_utf8(
        std::string bytes,
        SourceSite site = SourceSite::native()
    ) noexcept -> String {
        checked_utf8(bytes, site);
        return String(std::move(bytes));
    }

    constexpr auto size() const noexcept -> std::size_t { return as_str().size(); }

    constexpr auto empty() const noexcept -> bool { return as_str().empty(); }

    constexpr auto as_str() const noexcept -> std::string_view {
        if (const auto* owner = std::get_if<std::string>(&storage)) {
            return *owner;
        }
        return *std::get_if<std::string_view>(&storage);
    }

    constexpr auto append(std::string_view text) noexcept -> void { owned().append(text); }

    constexpr auto clear() noexcept -> void { owned().clear(); }

    constexpr auto push(char32_t scalar) noexcept -> void {
        const auto encoded = encode_valid_utf8(scalar);
        append(std::string_view(encoded.bytes.data(), encoded.width));
    }

    constexpr auto operator==(const String& other) const noexcept -> bool {
        return as_str() == other.as_str();
    }

private:
    explicit constexpr String(std::string_view text) noexcept
        : storage(std::in_place_type<std::string_view>, text) {}

    // Only validated ingress and proven formatting may adopt native storage.
    explicit constexpr String(std::string&& bytes) noexcept
        : storage(std::move(bytes)) {}

    // Generated static descriptors are immutable; writable values own their bytes.
    constexpr auto owned() noexcept -> std::string& { return *std::get_if<std::string>(&storage); }

    std::variant<std::string, std::string_view> storage;

    friend class StringFormatAccess;
    friend class FormatWriter;
};

} // namespace carven::runtime
