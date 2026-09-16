#include <carven/runtime/range.hpp>

static_assert([]() constexpr {
    auto sum = 0;
    for (const auto value : carven::runtime::Range<int> {1, 4, true}) {
        sum += value;
    }
    return sum == 10;
}());
