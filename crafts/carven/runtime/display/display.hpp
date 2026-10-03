#pragma once

#include "../format.hpp"
#include "../stateless.hpp"
#include "text.hpp"

#include <concepts>
#include <cstddef>
#include <functional>
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
    auto scalar(const T& value, std::size_t depth) noexcept -> void {
        if (truncate_at_limit(depth)) {
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
    // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward): Repeated calls borrow the same emitter as an lvalue.
    auto sequence(const Range& value, Emit&& emit, std::size_t depth) noexcept -> void {
        if (truncate_at_limit(depth)) {
            return;
        }
        text("[");
        auto count = std::size_t {0};
        for (const auto& element : value) {
            line(depth + 1);
            if (count == element_limit) {
                text("...,");
                break;
            }
            std::invoke(emit, *this, element, depth + 1);
            text(",");
            ++count;
            if (buffer.truncated()) {
                break;
            }
        }
        if (count != 0) {
            line(depth);
        }
        text("]");
    }

    template<typename Range, typename Emit>
    // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward): Both endpoints borrow the same emitter as an lvalue.
    auto range(const Range& value, Emit&& emit, std::size_t depth) noexcept -> void {
        if (truncate_at_limit(depth)) {
            return;
        }
        std::invoke(emit, *this, value.first, depth + 1);
        text(value.inclusive ? "..=" : "..");
        std::invoke(emit, *this, value.last, depth + 1);
    }

    auto result() const noexcept -> std::string_view { return buffer.result(); }

    // Writes the depth marker when this value cannot be expanded.
    auto truncate_at_limit(std::size_t depth) noexcept -> bool {
        if (depth >= depth_limit) {
            text("...");
            return true;
        }
        return false;
    }

    auto line(std::size_t depth) noexcept -> void { buffer.line(depth); }

private:
    static constexpr auto depth_limit = std::size_t {8};
    static constexpr auto element_limit = std::size_t {64};

    DisplayText buffer;
};

struct ScalarDisplay final {
    template<typename T>
    auto operator()(DisplayWriter& writer, const T& value, std::size_t depth) const noexcept
        -> void {
        writer.scalar(value, depth);
    }
};

template<Stateless Emit>
struct SequenceDisplay final {
    template<typename Range>
    auto operator()(DisplayWriter& writer, const Range& value, std::size_t depth) const noexcept
        -> void {
        writer.sequence(value, stateless_value<Emit>, depth);
    }
};

template<Stateless Emit>
struct RangeDisplay final {
    template<typename Range>
    auto operator()(DisplayWriter& writer, const Range& value, std::size_t depth) const noexcept
        -> void {
        writer.range(value, stateless_value<Emit>, depth);
    }
};

template<typename T, typename Emit>
    requires std::invocable<Emit&, DisplayWriter&, const T&, std::size_t>
struct StructuralDisplay final {
    const T& value;
    Emit& emit;
};

// Borrows the value and emitter until synchronous consumption completes.
// Temporaries remain alive only through the enclosing full expression.
template<typename T, typename Emit>
    requires std::invocable<Emit&, DisplayWriter&, const T&, std::size_t>
// NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward): Retains an lvalue borrow, including temporaries consumed synchronously.
auto structural_display(const T& value, Emit&& emit) noexcept
    -> StructuralDisplay<T, std::remove_reference_t<Emit>> {
    return {.value = value, .emit = emit};
}

} // namespace carven::runtime
