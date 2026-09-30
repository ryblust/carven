#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace carven::runtime {

// Bounded text storage for the native structural display component.
class DisplayText final {
public:
    static constexpr auto byte_limit = std::size_t {16384};
    static constexpr auto depth_limit = std::size_t {8};
    static constexpr auto element_limit = std::size_t {64};

    auto text(std::string_view value) noexcept -> void {
        if (is_truncated) {
            return;
        }
        constexpr auto limit = byte_limit;
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
            is_truncated = true;
            return;
        }
        bytes.append(value);
    }

    auto quoted(std::string_view value, bool character_literal = false) noexcept -> void {
        text(character_literal ? "'" : "\"");
        for (const auto character : value) {
            if (is_truncated) {
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

    auto result() const noexcept -> std::string_view { return bytes; }

    auto take() && noexcept -> std::string { return std::move(bytes); }

    auto truncated() const noexcept -> bool { return is_truncated; }

    auto line(std::size_t depth) noexcept -> void {
        text("\n");
        for (auto level = std::size_t {0}; level < depth; ++level) {
            text("    ");
        }
    }

private:
    std::string bytes;
    bool is_truncated = false;
};

} // namespace carven::runtime
