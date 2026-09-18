#pragma once

#include "format.hpp"

#include <string>
#include <string_view>
#include <type_traits>

namespace carven::runtime {

// A bounded logical display. It never invokes a user formatter or dereferences pointers.
class DisplayWriter final {
public:
    auto text(std::string_view value) noexcept -> void {
        if (truncated) {
            return;
        }
        constexpr auto limit = std::size_t {16384};
        if (value.size() > limit - bytes.size()) {
            bytes.append(value.substr(0, limit - bytes.size()));
            auto start = bytes.size();
            while (start != 0 && (static_cast<unsigned char>(bytes[start - 1]) & 0xc0u) == 0x80u) {
                --start;
            }
            if (start != 0) {
                --start;
                const auto lead = static_cast<unsigned char>(bytes[start]);
                const auto width = lead < 0x80u ? 1u : lead < 0xe0u ? 2u : lead < 0xf0u ? 3u : 4u;
                if (bytes.size() - start < width) {
                    bytes.resize(start);
                }
            }
            bytes.append("...");
            truncated = true;
            return;
        }
        bytes.append(value);
    }

    auto quoted(std::string_view value, bool character_literal = false) noexcept -> void {
        text(character_literal ? "'" : "\"");
        for (const auto character : value) {
            if (truncated) {
                break;
            }
            switch (character) {
                case '\\': text("\\\\"); break;
                case '"':  text(character_literal ? "\"" : "\\\""); break;
                case '\'': text(character_literal ? "\\'" : "'"); break;
                case '\n': text("\\n"); break;
                case '\r': text("\\r"); break;
                case '\t': text("\\t"); break;
                case '\0': text("\\0"); break;
                default:   text(std::string_view(&character, 1)); break;
            }
        }
        text(character_literal ? "'" : "\"");
    }

    template<typename T>
    auto scalar(const T& value) noexcept -> void {
        if constexpr (std::is_same_v<T, std::string_view>) {
            quoted(value);
        } else if constexpr (std::is_same_v<T, String>) {
            quoted(value.as_str());
        } else if constexpr (std::is_same_v<T, char32_t>) {
            quoted(carven::runtime::format_argument(value).as_str(), true);
        } else if constexpr (std::is_arithmetic_v<T>) {
            text(std::format("{}", carven::runtime::format_argument(value)));
        } else if constexpr (std::is_pointer_v<T>) {
            if (value == nullptr) {
                text("nullptr");
            } else {
                text(std::format("{}", static_cast<const void*>(value)));
            }
        } else {
            text("<opaque>");
        }
    }

    template<typename Range, typename Emit>
    auto sequence(const Range& value, Emit emit, std::size_t depth) noexcept -> void {
        text("[");
        auto count = std::size_t {0};
        for (const auto& element : value) {
            line(depth + 1);
            if (count == 64) {
                text("...,");
                break;
            }
            emit(*this, element);
            text(",");
            ++count;
            if (truncated) {
                break;
            }
        }
        if (count != 0) {
            line(depth);
        }
        text("]");
    }

    auto result() const noexcept -> std::string_view { return bytes; }

private:
    auto line(std::size_t depth) noexcept -> void {
        text("\n");
        for (auto level = std::size_t {0}; level < depth; ++level) {
            text("    ");
        }
    }

    std::string bytes;
    bool truncated = false;
};

template<typename T, typename Emit>
struct StructuralDisplay final {
    const T& value;
    Emit emit;
};

template<typename T, typename Emit>
auto structural_display(const T& value, Emit emit) noexcept -> StructuralDisplay<T, Emit> {
    return {.value = value, .emit = emit};
}

} // namespace carven::runtime
