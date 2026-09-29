module;
#include <carven/runtime/range.hpp>

module carven:test.internal.runtime.range;

import :test.harness.framework;
import std;

namespace {

namespace ct = carven::testing;

constexpr auto count_interval(carven::runtime::Range<std::uint64_t> range) noexcept -> int {
    auto count = 0;
    for (const auto value : range) {
        static_cast<void>(value);
        ++count;
        continue;
    }
    return count;
}

} // namespace

namespace {

const ct::Suite tests([] static noexcept {
    ct::test(
        "Runtime: integer intervals support constant evaluation and maximum endpoints",
        [] static noexcept {
            constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
            static_assert(count_interval({maximum - 1, maximum, true}) == 2);
            static_assert(count_interval({maximum, maximum, true}) == 1);
            static_assert(count_interval({maximum, maximum, false}) == 0);
            static_assert(count_interval({10, 0, true}) == 0);
            ct::expect(count_interval({maximum - 1, maximum, true}) == 2);
        }
    );
});

} // namespace
