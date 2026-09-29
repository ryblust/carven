module;
#include <carven/runtime/array.hpp>
#include <carven/runtime/index.hpp>
#include <carven/runtime/slice.hpp>

module carven:test.internal.runtime.index;

import :test.harness.framework;
import :test.internal.harness.death;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test(
        "Runtime: index offsets preserve integer width and reject invalid bounds",
        [] static noexcept {
            using carven::runtime::checked_index_offset;
            static_assert(checked_index_offset(std::uint8_t {255}, 300) == 255);
            static_assert(checked_index_offset(std::int8_t {127}, 300) == 127);
            static_assert(checked_index_offset(std::uint64_t {0}, 1) == 0);
            ct::expect(expect_termination("index-negative", []() static noexcept {
                auto values = std::array {1, 2};
                static_cast<void>(carven::runtime::checked_array_index(
                    values,
                    std::numeric_limits<std::int64_t>::min()
                ));
            }));
            ct::expect(expect_termination("index-upper-bound", []() static noexcept {
                const auto values = std::array {1, 2};
                static_cast<void>(carven::runtime::as_slice(values)[std::size_t {2}]);
            }));
            ct::expect(expect_termination("index-empty", []() static noexcept {
                static_cast<void>(checked_index_offset(0, 0));
            }));
            ct::expect(expect_termination("index-wide-unsigned", []() static noexcept {
                static_cast<void>(
                    checked_index_offset(std::numeric_limits<std::uint64_t>::max(), 2)
                );
            }));
        }
    );

    ct::test(
        "Runtime: slice indexing retains a read-only reference to its backing",
        [] static noexcept {
            const auto values = std::array {4, 6};
            const auto view = carven::runtime::as_slice(values);
            static_assert(std::same_as<decltype(view[0]), const int&>);
            ct::expect(std::addressof(view[1]) == std::addressof(values[1]));
        }
    );
});

} // namespace
