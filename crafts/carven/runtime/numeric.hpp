#pragma once

#include <bit>
#include <concepts>
#include <cstdlib>
#include <limits>
#include <type_traits>

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
    return detail::integer_from_bits<Type>(static_cast<Unsigned>(
        static_cast<Calculation>(detail::integer_bits(left))
        + static_cast<Calculation>(detail::integer_bits(right))
    ));
}

template<Integer Type>
constexpr auto integer_subtract(Type left, Type right) noexcept -> Type {
    using Unsigned = detail::UnsignedInteger<Type>;
    using Calculation = detail::UnsignedCalculation<Type>;
    return detail::integer_from_bits<Type>(static_cast<Unsigned>(
        static_cast<Calculation>(detail::integer_bits(left))
        - static_cast<Calculation>(detail::integer_bits(right))
    ));
}

template<Integer Type>
constexpr auto integer_multiply(Type left, Type right) noexcept -> Type {
    using Unsigned = detail::UnsignedInteger<Type>;
    using Calculation = detail::UnsignedCalculation<Type>;
    return detail::integer_from_bits<Type>(static_cast<Unsigned>(
        static_cast<Calculation>(detail::integer_bits(left))
        * static_cast<Calculation>(detail::integer_bits(right))
    ));
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

} // namespace carven::runtime
