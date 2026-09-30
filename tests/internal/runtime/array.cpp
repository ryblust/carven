module;
#include <carven/runtime/array.hpp>

module carven:test.internal.runtime.array;

import :test.harness.framework;
import std;

namespace {

constexpr auto site = carven::runtime::SourceSite::native();

namespace ct = carven::testing;

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

namespace {

const ct::Suite tests([] static noexcept {
    ct::test("Runtime: checked array indexing preserves references", [] static noexcept {
        using namespace carven::runtime;
        auto values = std::array<std::int32_t, 3> {1, 2, 3};
        checked_array_index(values, std::int32_t {0}, site) = 4;
        checked_array_index(values, std::size_t {2}, site) = 6;
        ct::expect_equal(values[0], 4);
        ct::expect_equal(values[2], 6);
    });


    ct::test("Runtime: array adoption directly constructs ordered elements", [] static noexcept {
        auto trace = std::vector<int>();
        {
            const auto source = std::array {ArrayElement {1, &trace}, ArrayElement {2, &trace}};
            const auto adopted =
                carven::runtime::adopt_array<std::array<AdoptedArrayElement, 2>, false>(source);
            ct::expect_equal(adopted.size(), 2uz);
            ct::expect(trace == std::vector<int> {1, 2});
        }
        ct::expect(trace == std::vector<int> {1, 2, -2, -1});
    });
});

} // namespace
