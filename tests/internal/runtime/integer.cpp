module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>
#include <carven/runtime/runtime.hpp>
#include <csignal>
#include <concepts>
#include <cstdio>
#include <limits>
#include <sys/wait.h>
#include <unistd.h>

module carven:test.internal.runtime.integer;

namespace {

template<typename Function>
auto terminates(Function function) noexcept -> bool {
    std::fflush(nullptr);
    const auto child = fork();
    if (child == 0) {
        std::signal(SIGABRT, SIG_DFL);
        function();
        _exit(0);
    }
    if (child < 0) {
        return false;
    }
    auto status = 0;
    if (waitpid(child, &status, 0) != child) {
        return false;
    }
    return WIFSIGNALED(status) || (WIFEXITED(status) && WEXITSTATUS(status) != 0);
}

template<typename Integer>
constexpr auto ordinary_integer_contract() noexcept -> bool {
    using namespace carven::runtime;
    auto value = Integer {6};
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
    && integer_divide(Integer {7}, Integer {2}) == Integer {3}
    && integer_remainder(Integer {7}, Integer {2}) == Integer {1}
    && integer_left_shift(Integer {3}, Integer {2}) == Integer {12}
    && integer_right_shift(Integer {12}, Integer {2}) == Integer {3}
    && &integer_add_assign(value, Integer {1}) == &value
        && value == Integer {7};
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

    auto incremented = maximum;
    auto decremented = minimum;
    auto added = maximum;
    auto subtracted = minimum;
    auto multiplied_assigned = maximum;
    auto shifted = maximum;
    const auto mutations_wrap = integer_increment(incremented) == minimum
        && integer_decrement(decremented) == maximum
        && integer_add_assign(added, Integer {1}) == minimum
        && integer_subtract_assign(subtracted, Integer {1}) == maximum
        && integer_multiply_assign(multiplied_assigned, Integer {2}) == multiplied
        && integer_left_shift_assign(shifted, Integer {1}) == multiplied;

    if constexpr (std::signed_integral<Integer>) {
        auto divided = minimum;
        auto remainder = minimum;
        return mutations_wrap
            && integer_add(maximum, Integer {1}) == minimum
            && integer_subtract(minimum, Integer {1}) == maximum
            && integer_multiply(maximum, Integer {2}) == multiplied
            && integer_negate(minimum) == minimum
            && integer_divide(minimum, Integer {-1}) == minimum
            && integer_remainder(minimum, Integer {-1}) == Integer {0}
        && integer_divide_assign(divided, Integer {-1}) == minimum
            && integer_remainder_assign(remainder, Integer {-1}) == Integer {0}
        && integer_left_shift(maximum, Integer {1}) == multiplied;
    } else {
        return mutations_wrap
            && integer_add(maximum, Integer {1}) == Integer {0}
        && integer_subtract(Integer {0}, Integer {1}) == maximum
            && integer_multiply(maximum, Integer {2}) == multiplied
            && integer_negate(Integer {1}) == maximum
            && integer_left_shift(maximum, Integer {1}) == multiplied;
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

} // namespace

TEST_CASE("Runtime: integer helpers cover every fixed width") {
    static_assert(ordinary_integer_contract<std::int8_t>());
    static_assert(ordinary_integer_contract<std::int16_t>());
    static_assert(ordinary_integer_contract<std::int32_t>());
    static_assert(ordinary_integer_contract<std::int64_t>());
    static_assert(ordinary_integer_contract<std::uint8_t>());
    static_assert(ordinary_integer_contract<std::uint16_t>());
    static_assert(ordinary_integer_contract<std::uint32_t>());
    static_assert(ordinary_integer_contract<std::uint64_t>());
    CHECK(ordinary_integer_contract<std::ptrdiff_t>());
    CHECK(ordinary_integer_contract<std::size_t>());
}

TEST_CASE("Runtime: signed right shift is arithmetic and mutation helpers return the target") {
    using namespace carven::runtime;
    static_assert(integer_right_shift(std::int8_t {-1}, std::int8_t {1}) == std::int8_t {-1});
    static_assert(integer_right_shift(std::int16_t {-8}, std::int16_t {2}) == std::int16_t {-2});
    static_assert(integer_right_shift(std::int32_t {-7}, std::int32_t {1}) == std::int32_t {-4});
    static_assert(integer_right_shift(std::int64_t {-7}, std::int64_t {1}) == std::int64_t {-4});

    auto value = std::int32_t {4};
    CHECK_EQ(integer_multiply_assign(value, std::int32_t {3}), std::int32_t {12});
    CHECK_EQ(integer_left_shift_assign(value, std::int32_t {1}), std::int32_t {24});
    CHECK_EQ(integer_decrement(value), std::int32_t {23});
    CHECK_EQ(integer_increment(value), std::int32_t {24});
}

TEST_CASE("Runtime: invalid arithmetic and array indexing always terminate") {
    using namespace carven::runtime;
    CHECK(terminates([]() static noexcept {
        static_cast<void>(integer_divide(std::int32_t {1}, std::int32_t {0}));
    }));
    CHECK(terminates([]() static noexcept {
        static_cast<void>(integer_remainder(std::int32_t {1}, std::int32_t {0}));
    }));
    CHECK(terminates([]() static noexcept {
        static_cast<void>(integer_left_shift(std::int32_t {1}, std::int32_t {-1}));
    }));
    CHECK(terminates([]() static noexcept {
        static_cast<void>(integer_right_shift(std::int32_t {1}, std::int32_t {32}));
    }));
    CHECK(terminates([]() static noexcept {
        auto values = std::array<std::int32_t, 2> {1, 2};
        static_cast<void>(checked_array_index(values, std::int32_t {-1}));
    }));
    CHECK(terminates([]() static noexcept {
        auto values = std::array<std::int32_t, 2> {1, 2};
        static_cast<void>(checked_array_index(values, std::size_t {2}));
    }));
}

TEST_CASE("Runtime: checked array indexing preserves references") {
    using namespace carven::runtime;
    auto values = std::array<std::int32_t, 3> {1, 2, 3};
    checked_array_index(values, std::int32_t {0}) = 4;
    checked_array_index(values, std::size_t {2}) = 6;
    CHECK_EQ(values[0], 4);
    CHECK_EQ(values[2], 6);
}
