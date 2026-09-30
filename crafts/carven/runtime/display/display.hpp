#pragma once

#include "../format.hpp"
#include "text.hpp"

#include <string>
#include <string_view>
#include <type_traits>

namespace carven::runtime {

// A bounded logical display. Only C string pointers are read as text;
// user formatters are never invoked.
class DisplayWriter final {
public:
    auto text(std::string_view value) noexcept -> void { buffer.text(value); }

    auto quoted(std::string_view value, bool character_literal = false) noexcept -> void {
        buffer.quoted(value, character_literal);
    }

    template<typename T>
    auto scalar(const T& value) noexcept -> void {
        if (depth == DisplayText::depth_limit) {
            text("...");
            return;
        }
        if constexpr (std::is_same_v<T, std::string_view>) {
            quoted(value);
        } else if constexpr (std::is_same_v<T, String>) {
            quoted(value.as_str());
        } else if constexpr (std::is_same_v<T, const char*> || std::is_same_v<T, char*>) {
            if (value == nullptr) {
                text("nullptr");
            } else {
                quoted(value);
            }
        } else if constexpr (std::is_same_v<T, char32_t>) {
            quoted(carven::runtime::format_argument(value).as_str(), true);
        } else if constexpr (std::is_same_v<T, char8_t>
                             || std::is_same_v<T, char16_t>
                             || std::is_same_v<T, wchar_t>) {
            text(std::format("{}", +value));
        } else if constexpr (std::is_arithmetic_v<T>) {
            text(std::format("{}", carven::runtime::format_argument(value)));
        } else if constexpr (std::is_pointer_v<T> && std::is_convertible_v<T, const void*>) {
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
    auto sequence(const Range& value, Emit emit) noexcept -> void {
        if (!enter()) {
            return;
        }
        text("[");
        auto count = std::size_t {0};
        for (const auto& element : value) {
            line();
            if (count == DisplayText::element_limit) {
                text("...,");
                break;
            }
            emit(*this, element);
            text(",");
            ++count;
            if (buffer.truncated()) {
                break;
            }
        }
        if (count != 0) {
            line(1);
        }
        text("]");
        leave();
    }

    auto result() const noexcept -> std::string_view { return buffer.result(); }

    auto enter() noexcept -> bool {
        if (depth == DisplayText::depth_limit) {
            text("...");
            return false;
        }
        ++depth;
        return true;
    }

    auto leave() noexcept -> void { --depth; }

    auto line(std::size_t outer = 0) noexcept -> void { buffer.line(depth - outer); }

private:
    DisplayText buffer;
    std::size_t depth = 0;
};

struct ScalarDisplay final {
    template<typename T>
    auto operator()(DisplayWriter& writer, const T& value) const noexcept -> void {
        writer.scalar(value);
    }
};

template<typename Emit>
struct SequenceDisplay final {
    template<typename Range>
    auto operator()(DisplayWriter& writer, const Range& value) const noexcept -> void {
        writer.sequence(value, Emit {});
    }
};

template<typename Emit>
struct RangeDisplay final {
    template<typename Range>
    auto operator()(DisplayWriter& writer, const Range& value) const noexcept -> void {
        if (!writer.enter()) {
            return;
        }
        Emit {}(writer, value.first);
        writer.text(value.inclusive ? "..=" : "..");
        Emit {}(writer, value.last);
        writer.leave();
    }
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
