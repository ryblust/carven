module;
#include <carven/runtime/numeric.hpp>
#include <concepts>
#include <limits>

module carven:test.internal.runtime.numeric;

import :test.harness.framework;
import :test.internal.harness.death;

namespace {

constexpr auto site = carven::runtime::SourceSite::native();

template<typename Integer>
constexpr auto ordinary_integer_contract() noexcept -> bool {
    using namespace carven::runtime;
    const auto negation_matches = [&]() noexcept {
        if constexpr (std::signed_integral<Integer>) {
            return integer_negate(Integer {2}) == Integer {-2};
        } else {
            return true;
        }
    }();
    return negation_matches
        && integer_add(Integer {2}, Integer {3}) == Integer {5}
    && integer_subtract(Integer {5}, Integer {3}) == Integer {2}
    && integer_multiply(Integer {6}, Integer {7}) == Integer {42}
    && integer_divide(Integer {7}, Integer {2}, site) == Integer {3}
    && integer_remainder(Integer {7}, Integer {2}, site) == Integer {1}
    && integer_left_shift(Integer {3}, Integer {2}, site) == Integer {12}
    && integer_right_shift(Integer {12}, Integer {2}, site) == Integer {3};
}

template<carven::runtime::Integer Integer>
constexpr auto wrapping_integer_contract() noexcept -> bool {
    using namespace carven::runtime;
    constexpr auto minimum = std::numeric_limits<Integer>::min();
    constexpr auto maximum = std::numeric_limits<Integer>::max();
    constexpr auto multiplied = []() static noexcept -> Integer {
        if constexpr (std::signed_integral<Integer>) {
            return Integer {-2};
        } else {
            return static_cast<Integer>(std::numeric_limits<Integer>::max() - Integer {1});
        }
    }();

    if constexpr (std::signed_integral<Integer>) {
        return integer_add(maximum, Integer {1}) == minimum
            && integer_subtract(minimum, Integer {1}) == maximum
            && integer_multiply(maximum, Integer {2}) == multiplied
            && integer_negate(minimum) == minimum
            && integer_divide(minimum, Integer {-1}, site) == minimum
            && integer_remainder(minimum, Integer {-1}, site) == Integer {0}
        && integer_left_shift(maximum, Integer {1}, site) == multiplied;
    } else {
        return integer_add(maximum, Integer {1}) == Integer {0}
        && integer_subtract(Integer {0}, Integer {1}) == maximum
            && integer_multiply(maximum, Integer {2}) == multiplied
            && integer_negate(Integer {1}) == maximum
            && integer_left_shift(maximum, Integer {1}, site) == multiplied;
    }
}

static_assert(wrapping_integer_contract<std::int8_t>());
static_assert(wrapping_integer_contract<std::int16_t>());
static_assert(wrapping_integer_contract<std::int32_t>());
static_assert(wrapping_integer_contract<std::int64_t>());
static_assert(wrapping_integer_contract<std::uint8_t>());
static_assert(wrapping_integer_contract<std::uint16_t>());
static_assert(wrapping_integer_contract<std::uint32_t>());
static_assert(wrapping_integer_contract<std::uint64_t>());
static_assert(wrapping_integer_contract<std::ptrdiff_t>());
static_assert(wrapping_integer_contract<std::size_t>());

// Signed right shift retains the arithmetic result during constant evaluation.
static_assert(
    carven::runtime::integer_right_shift(std::int8_t {-1}, std::int8_t {1}, site)
    == std::int8_t {-1}
);
static_assert(
    carven::runtime::integer_right_shift(std::int16_t {-8}, std::int16_t {2}, site)
    == std::int16_t {-2}
);
static_assert(
    carven::runtime::integer_right_shift(std::int32_t {-7}, std::int32_t {1}, site)
    == std::int32_t {-4}
);
static_assert(
    carven::runtime::integer_right_shift(std::int64_t {-7}, std::int64_t {1}, site)
    == std::int64_t {-4}
);


const TestSuite suite([] static noexcept {
    "Runtime: integer helpers cover every fixed width"_test = [] static noexcept {
        static_assert(ordinary_integer_contract<std::int8_t>());
        static_assert(ordinary_integer_contract<std::int16_t>());
        static_assert(ordinary_integer_contract<std::int32_t>());
        static_assert(ordinary_integer_contract<std::int64_t>());
        static_assert(ordinary_integer_contract<std::uint8_t>());
        static_assert(ordinary_integer_contract<std::uint16_t>());
        static_assert(ordinary_integer_contract<std::uint32_t>());
        static_assert(ordinary_integer_contract<std::uint64_t>());
        expect(ordinary_integer_contract<std::ptrdiff_t>());
        expect(ordinary_integer_contract<std::size_t>());
    };

    "Runtime: invalid arithmetic always terminates"_test = [] static noexcept {
        using namespace carven::runtime;
        expect(expect_termination("runtime-integer-divide-zero", []() static noexcept {
            static_cast<void>(integer_divide(std::int32_t {1}, std::int32_t {0}, site));
        }));
        expect(expect_termination("runtime-integer-remainder-zero", []() static noexcept {
            static_cast<void>(integer_remainder(std::int32_t {1}, std::int32_t {0}, site));
        }));
        expect(expect_termination("runtime-left-shift-negative", []() static noexcept {
            static_cast<void>(integer_left_shift(std::int32_t {1}, std::int32_t {-1}, site));
        }));
        expect(expect_termination("runtime-right-shift-width", []() static noexcept {
            static_cast<void>(integer_right_shift(std::int32_t {1}, std::int32_t {32}, site));
        }));
    };
});

} // namespace
