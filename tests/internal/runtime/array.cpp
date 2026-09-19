module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>
#include <carven/runtime/array.hpp>

module carven:test.internal.runtime.array;

import std;

TEST_CASE("Runtime: checked array indexing preserves references") {
    using namespace carven::runtime;
    auto values = std::array<std::int32_t, 3> {1, 2, 3};
    checked_array_index(values, std::int32_t {0}) = 4;
    checked_array_index(values, std::size_t {2}) = 6;
    CHECK_EQ(values[0], 4);
    CHECK_EQ(values[2], 6);
}

namespace {

struct ArrayElement final {
    int value;
    std::vector<int>* trace;
};

class AdoptedArrayElement final {
public:
    explicit AdoptedArrayElement(const ArrayElement& source) noexcept;
    ~AdoptedArrayElement() noexcept;
    AdoptedArrayElement(const AdoptedArrayElement&) = delete;
    AdoptedArrayElement(AdoptedArrayElement&&) = delete;
    auto operator=(const AdoptedArrayElement&) -> AdoptedArrayElement& = delete;
    auto operator=(AdoptedArrayElement&&) -> AdoptedArrayElement& = delete;

private:
    ArrayElement source;
};

AdoptedArrayElement::AdoptedArrayElement(const ArrayElement& input) noexcept
    : source(input) {
    source.trace->push_back(source.value);
}

AdoptedArrayElement::~AdoptedArrayElement() noexcept {
    source.trace->push_back(-source.value);
}

} // namespace

TEST_CASE("Runtime: array adoption directly constructs ordered elements") {
    auto trace = std::vector<int>();
    {
        const auto source = std::array {ArrayElement {1, &trace}, ArrayElement {2, &trace}};
        const auto adopted =
            carven::runtime::adopt_array<std::array<AdoptedArrayElement, 2>, false>(source);
        CHECK_EQ(adopted.size(), 2);
        CHECK(trace == std::vector<int> {1, 2});
    }
    CHECK(trace == std::vector<int> {1, 2, -2, -1});
}
