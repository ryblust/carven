module;
#include <carven/runtime/range.hpp>

module carven:test.internal.runtime.range;

import :test.harness.framework;
import std;

namespace {

constexpr auto count_interval(carven::runtime::Range<std::uint64_t> range) noexcept -> int {
    auto count = 0;
    for (const auto value : range) {
        static_cast<void>(value);
        ++count;
        continue;
    }
    return count;
}

const TestSuite suite([] static noexcept {
    "Runtime: integer intervals support static execution and maximum endpoints"_test =
        [] static noexcept {
            constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
            static_assert(count_interval({maximum - 1, maximum, true}) == 2);
            static_assert(count_interval({maximum, maximum, true}) == 1);
            static_assert(count_interval({maximum, maximum, false}) == 0);
            static_assert(count_interval({10, 0, true}) == 0);
            expect(count_interval({maximum - 1, maximum, true}) == 2);
        };
});

} // namespace
