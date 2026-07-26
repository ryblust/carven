#pragma once

#include "callable.hpp"
#include "outcome.hpp"

#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <optional>
#include <ranges>
#include <string_view>
#include <type_traits>
#include <utility>

namespace carven::runtime {

static_assert(
    sizeof(float) == 4
    && std::numeric_limits<float>::digits == 24
    && std::numeric_limits<float>::min_exponent == -125
    && std::numeric_limits<float>::max_exponent == 128
    && std::numeric_limits<float>::is_iec559
);
static_assert(
    sizeof(double) == 8
    && std::numeric_limits<double>::digits == 53
    && std::numeric_limits<double>::min_exponent == -1021
    && std::numeric_limits<double>::max_exponent == 1024
    && std::numeric_limits<double>::is_iec559
);

template<typename Type>
concept Integer = std::integral<Type>
    && !std::same_as<std::remove_cv_t<Type>, bool>
    && !std::same_as<std::remove_cv_t<Type>, char>
    && !std::same_as<std::remove_cv_t<Type>, wchar_t>
    && !std::same_as<std::remove_cv_t<Type>, char8_t>
    && !std::same_as<std::remove_cv_t<Type>, char16_t>
    && !std::same_as<std::remove_cv_t<Type>, char32_t>;

namespace detail {

[[noreturn]] inline auto unicode_contract_error(std::string_view detail) noexcept -> void {
    std::fputs("carven runtime contract error: ", stderr);
    std::fwrite(detail.data(), sizeof(char), detail.size(), stderr);
    std::fputc('\n', stderr);
    std::abort();
}

struct DecodedUTF8 final {
    char32_t scalar;
    std::size_t width;
};

constexpr auto try_decode_utf8(const char* current, const char* end) noexcept
    -> std::optional<DecodedUTF8> {
    if (current == end) {
        return std::nullopt;
    }
    const auto first = static_cast<unsigned char>(*current);
    if (first < 0x80) {
        return DecodedUTF8 {.scalar = static_cast<char32_t>(first), .width = 1};
    }
    const auto remaining = static_cast<std::size_t>(end - current);
    const auto continuation =
        [&](std::size_t offset) constexpr noexcept -> std::optional<unsigned char> {
        if (offset >= remaining) {
            return std::nullopt;
        }
        const auto byte = static_cast<unsigned char>(current[offset]);
        if ((byte & 0xc0u) != 0x80u) {
            return std::nullopt;
        }
        return byte;
    };
    if (first >= 0xc2 && first <= 0xdf) {
        const auto second = continuation(1);
        if (!second.has_value()) {
            return std::nullopt;
        }
        return {
            DecodedUTF8 {
                .scalar = static_cast<char32_t>(((first & 0x1fu) << 6) | (*second & 0x3fu)),
                .width = 2,
            },
        };
    }
    if (first >= 0xe0 && first <= 0xef) {
        const auto second = continuation(1);
        const auto third = continuation(2);
        if (!second.has_value()
            || !third.has_value()
            || (first == 0xe0 && *second < 0xa0)
            || (first == 0xed && *second >= 0xa0)) {
            return std::nullopt;
        }
        return {
            DecodedUTF8 {
                .scalar = static_cast<char32_t>(
                    ((first & 0x0fu) << 12) | ((*second & 0x3fu) << 6) | (*third & 0x3fu)
                ),
                .width = 3,
            },
        };
    }
    if (first >= 0xf0 && first <= 0xf4) {
        const auto second = continuation(1);
        const auto third = continuation(2);
        const auto fourth = continuation(3);
        if (!second.has_value()
            || !third.has_value()
            || !fourth.has_value()
            || (first == 0xf0 && *second < 0x90)
            || (first == 0xf4 && *second >= 0x90)) {
            return std::nullopt;
        }
        return {
            DecodedUTF8 {
                .scalar = static_cast<char32_t>(
                    ((first & 0x07u) << 18) | ((*second & 0x3fu) << 12) | ((*third & 0x3fu) << 6)
                    | (*fourth & 0x3fu)
                ),
                .width = 4,
            },
        };
    }
    return std::nullopt;
}

constexpr auto decode_utf8(const char* current, const char* end) noexcept -> DecodedUTF8 {
    const auto decoded = try_decode_utf8(current, end);
    if (decoded.has_value()) {
        return *decoded;
    }
    unicode_contract_error("invalid UTF-8 from typed #[cpp]");
}

template<std::size_t Extent, Integer Index>
constexpr auto checked_array_offset(Index index) noexcept -> std::size_t {
    if constexpr (std::signed_integral<Index>) {
        if (index < 0) {
            std::abort();
        }
    }
    if (static_cast<std::make_unsigned_t<Index>>(index) >= Extent) {
        std::abort();
    }
    return static_cast<std::size_t>(index);
}

template<Integer Type>
using UnsignedInteger = std::make_unsigned_t<Type>;

template<Integer Type>
using UnsignedCalculation = std::conditional_t<
    (sizeof(UnsignedInteger<Type>) < sizeof(unsigned int)),
    unsigned int,
    UnsignedInteger<Type>>;

template<Integer Type>
constexpr auto integer_bits(Type value) noexcept -> UnsignedInteger<Type> {
    return static_cast<UnsignedInteger<Type>>(value);
}

template<Integer Type>
constexpr auto integer_from_bits(UnsignedInteger<Type> bits) noexcept -> Type {
    if constexpr (std::unsigned_integral<Type>) {
        return bits;
    } else {
        return std::bit_cast<Type>(bits);
    }
}

} // namespace detail

template<Integer Type>
constexpr auto integer_negate(Type operand) noexcept -> Type {
    using Calculation = detail::UnsignedCalculation<Type>;
    const auto bits = static_cast<detail::UnsignedInteger<Type>>(
        Calculation {0} - static_cast<Calculation>(detail::integer_bits(operand))
    );
    return detail::integer_from_bits<Type>(bits);
}

template<Integer Type>
constexpr auto integer_add(Type left, Type right) noexcept -> Type {
    using Unsigned = detail::UnsignedInteger<Type>;
    using Calculation = detail::UnsignedCalculation<Type>;
    const auto left_bits = detail::integer_bits(left);
    const auto right_bits = detail::integer_bits(right);
    const auto result = static_cast<Unsigned>(
        static_cast<Calculation>(left_bits) + static_cast<Calculation>(right_bits)
    );
    return detail::integer_from_bits<Type>(result);
}

template<Integer Type>
constexpr auto integer_subtract(Type left, Type right) noexcept -> Type {
    using Unsigned = detail::UnsignedInteger<Type>;
    using Calculation = detail::UnsignedCalculation<Type>;
    const auto left_bits = detail::integer_bits(left);
    const auto right_bits = detail::integer_bits(right);
    const auto result = static_cast<Unsigned>(
        static_cast<Calculation>(left_bits) - static_cast<Calculation>(right_bits)
    );
    return detail::integer_from_bits<Type>(result);
}

template<Integer Type>
constexpr auto integer_multiply(Type left, Type right) noexcept -> Type {
    using Unsigned = detail::UnsignedInteger<Type>;
    using Calculation = detail::UnsignedCalculation<Type>;
    const auto left_bits = detail::integer_bits(left);
    const auto right_bits = detail::integer_bits(right);
    const auto result = static_cast<Unsigned>(
        static_cast<Calculation>(left_bits) * static_cast<Calculation>(right_bits)
    );
    return detail::integer_from_bits<Type>(result);
}

template<Integer Type>
constexpr auto integer_divide(Type left, Type right) noexcept -> Type {
    if (right == 0) {
        std::abort();
    }
    if constexpr (std::signed_integral<Type>) {
        if (left == std::numeric_limits<Type>::min() && right == Type {-1}) {
            return std::numeric_limits<Type>::min();
        }
    }
    return static_cast<Type>(left / right);
}

template<Integer Type>
constexpr auto integer_remainder(Type left, Type right) noexcept -> Type {
    if (right == 0) {
        std::abort();
    }
    if constexpr (std::signed_integral<Type>) {
        if (left == std::numeric_limits<Type>::min() && right == Type {-1}) {
            return Type {0};
        }
    }
    return static_cast<Type>(left % right);
}

template<Integer Type, Integer Count>
constexpr auto integer_left_shift(Type left, Count count) noexcept -> Type {
    using Unsigned = detail::UnsignedInteger<Type>;
    constexpr auto width = std::numeric_limits<Unsigned>::digits;
    if constexpr (std::signed_integral<Count>) {
        if (count < 0) {
            std::abort();
        }
    }
    const auto unsigned_count = static_cast<std::make_unsigned_t<Count>>(count);
    if (unsigned_count >= width) {
        std::abort();
    }
    using Calculation = detail::UnsignedCalculation<Type>;
    return detail::integer_from_bits<Type>(static_cast<Unsigned>(
        static_cast<Calculation>(detail::integer_bits(left)) << unsigned_count
    ));
}

template<Integer Type, Integer Count>
constexpr auto integer_right_shift(Type left, Count count) noexcept -> Type {
    using Unsigned = detail::UnsignedInteger<Type>;
    constexpr auto width = std::numeric_limits<Unsigned>::digits;
    if constexpr (std::signed_integral<Count>) {
        if (count < 0) {
            std::abort();
        }
    }
    const auto unsigned_count = static_cast<std::make_unsigned_t<Count>>(count);
    if (unsigned_count >= width) {
        std::abort();
    }
    const auto bits = detail::integer_bits(left);
    if constexpr (std::unsigned_integral<Type>) {
        return static_cast<Type>(bits >> unsigned_count);
    } else {
        if (left >= 0 || unsigned_count == 0) {
            return detail::integer_from_bits<Type>(static_cast<Unsigned>(bits >> unsigned_count));
        }
        using Calculation = detail::UnsignedCalculation<Type>;
        const auto high_bits = static_cast<Unsigned>(
            static_cast<Calculation>(std::numeric_limits<Unsigned>::max())
            << (width - unsigned_count)
        );
        return detail::integer_from_bits<Type>(
            static_cast<Unsigned>((bits >> unsigned_count) | high_bits)
        );
    }
}

template<Integer Type>
constexpr auto integer_add_assign(Type& target, Type value) noexcept -> Type& {
    target = integer_add(target, value);
    return target;
}

template<Integer Type>
constexpr auto integer_subtract_assign(Type& target, Type value) noexcept -> Type& {
    target = integer_subtract(target, value);
    return target;
}

template<Integer Type>
constexpr auto integer_multiply_assign(Type& target, Type value) noexcept -> Type& {
    target = integer_multiply(target, value);
    return target;
}

template<Integer Type>
constexpr auto integer_divide_assign(Type& target, Type value) noexcept -> Type& {
    target = integer_divide(target, value);
    return target;
}

template<Integer Type>
constexpr auto integer_remainder_assign(Type& target, Type value) noexcept -> Type& {
    target = integer_remainder(target, value);
    return target;
}

template<Integer Type, Integer Count>
constexpr auto integer_left_shift_assign(Type& target, Count count) noexcept -> Type& {
    target = integer_left_shift(target, count);
    return target;
}

template<Integer Type, Integer Count>
constexpr auto integer_right_shift_assign(Type& target, Count count) noexcept -> Type& {
    target = integer_right_shift(target, count);
    return target;
}

template<Integer Type>
constexpr auto integer_increment(Type& target) noexcept -> Type& {
    target = integer_add(target, Type {1});
    return target;
}

template<Integer Type>
constexpr auto integer_decrement(Type& target) noexcept -> Type& {
    target = integer_subtract(target, Type {1});
    return target;
}

template<typename Element, std::size_t Extent, Integer Index>
constexpr auto checked_array_index(std::array<Element, Extent>& array, Index index) noexcept
    -> Element& {
    return array[detail::checked_array_offset<Extent>(index)];
}

template<typename Element, std::size_t Extent, Integer Index>
constexpr auto checked_array_index(const std::array<Element, Extent>& array, Index index) noexcept
    -> const Element& {
    return array[detail::checked_array_offset<Extent>(index)];
}

class StrBytesView final {
public:
    class Iterator final {
    public:
        constexpr explicit Iterator(const char* current) noexcept
            : current(current) {}
        constexpr auto operator*() const noexcept -> std::uint8_t {
            return static_cast<std::uint8_t>(static_cast<unsigned char>(*current));
        }
        constexpr auto operator++() noexcept -> Iterator& {
            ++current;
            return *this;
        }
        constexpr auto operator!=(const Iterator& other) const noexcept -> bool {
            return current != other.current;
        }

    private:
        const char* current;
    };

    constexpr explicit StrBytesView(std::string_view text) noexcept
        : text(text) {}
    // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage): this iterator is bounded by end(), not a C string.
    constexpr auto begin() const noexcept -> Iterator { return Iterator(text.data()); }
    constexpr auto end() const noexcept -> Iterator { return Iterator(text.data() + text.size()); }

private:
    std::string_view text;
};

class StrCharsView final {
public:
    class Iterator final {
    public:
        constexpr Iterator(const char* current, const char* end) noexcept
            : current(current),
              end_pointer(end) {}
        constexpr auto operator*() const noexcept -> char32_t {
            return detail::decode_utf8(current, end_pointer).scalar;
        }
        constexpr auto operator++() noexcept -> Iterator& {
            current += detail::decode_utf8(current, end_pointer).width;
            return *this;
        }
        constexpr auto operator!=(const Iterator& other) const noexcept -> bool {
            return current != other.current;
        }

    private:
        const char* current;
        const char* end_pointer;
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

constexpr auto str_bytes(std::string_view text) noexcept -> StrBytesView {
    return StrBytesView(text);
}

constexpr auto str_chars(std::string_view text) noexcept -> StrCharsView {
    return StrCharsView(text);
}

constexpr auto checked_foreign_str(std::string_view text) noexcept -> std::string_view {
    const auto* current = text.data();
    const auto* end = current + text.size();
    while (current != end) {
        current += detail::decode_utf8(current, end).width;
    }
    return text;
}

constexpr auto checked_foreign_char(char32_t value) noexcept -> char32_t {
    if (value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) {
        detail::unicode_contract_error("invalid Unicode scalar from typed #[cpp]");
    }
    return value;
}

template<typename Subject, typename Value>
constexpr auto is(Subject&& subject, Value&& value) noexcept(
    noexcept(std::forward<Subject>(subject) == std::forward<Value>(value))
) -> bool {
    return std::forward<Subject>(subject) == std::forward<Value>(value);
}

template<typename Constraint, typename Subject>
constexpr auto is(Subject&&) noexcept -> bool {
    return std::is_same_v<std::remove_cvref_t<Subject>, Constraint>;
}

template<std::integral Integer>
    requires (!std::same_as<Integer, bool>)
constexpr auto integer_range(Integer begin, Integer end) noexcept -> auto {
    return std::views::iota(begin, begin < end ? end : begin);
}

constexpr auto entry_args(int argc, const char* const* argv) noexcept -> auto {
    const auto count = argc > 1 ? static_cast<std::size_t>(argc - 1) : std::size_t {0};
    return std::views::iota(std::size_t {0}, count)
        | std::views::transform([argv](std::size_t index) noexcept {
               return std::pair {index, std::string_view(argv[index + 1])};
           });
}

}
