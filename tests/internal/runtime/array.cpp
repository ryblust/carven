module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>
#include <carven/runtime/array.hpp>

module carven:test.internal.runtime.array;

import :test.internal.harness.death;

TEST_CASE("Runtime: invalid array indexing always terminates") {
    using namespace carven::runtime;
    CHECK(expect_termination("runtime-array-negative-index", []() static noexcept {
        auto values = std::array<std::int32_t, 2> {1, 2};
        static_cast<void>(checked_array_index(values, std::int32_t {-1}));
    }));
    CHECK(expect_termination("runtime-array-upper-bound-index", []() static noexcept {
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
