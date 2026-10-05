module;
#include <carven/runtime/array.hpp>
#include <carven/runtime/index.hpp>
#include <carven/runtime/slice.hpp>

module carven:test.internal.runtime.index;

import :test.harness.framework;
import :test.internal.harness.death;
import std;

namespace {

constexpr auto site = carven::runtime::SourceSite::native();

const TestSuite suite([] static noexcept {
    "Runtime: index offsets preserve integer width and reject invalid bounds"_test =
        [] static noexcept {
            using carven::runtime::checked_index_offset;
            static_assert(checked_index_offset(std::uint8_t {255}, 300, site) == 255);
            static_assert(checked_index_offset(std::int8_t {127}, 300, site) == 127);
            static_assert(checked_index_offset(std::uint64_t {0}, 1, site) == 0);
            expect(expect_termination("index-negative", []() static noexcept {
                auto values = std::array {1, 2};
                static_cast<void>(carven::runtime::checked_array_index(
                    values,
                    std::numeric_limits<std::int64_t>::min(),
                    site
                ));
            }));
            expect(expect_termination("index-upper-bound", []() static noexcept {
                const auto values = std::array {1, 2};
                static_cast<void>(carven::runtime::as_slice(values)[std::size_t {2}]);
            }));
            expect(expect_termination("index-empty", []() static noexcept {
                static_cast<void>(checked_index_offset(0, 0, site));
            }));
            expect(expect_termination("index-wide-unsigned", []() static noexcept {
                static_cast<void>(
                    checked_index_offset(std::numeric_limits<std::uint64_t>::max(), 2, site)
                );
            }));
        };

    "Runtime: slice indexing retains a read-only reference to its backing"_test =
        [] static noexcept {
            const auto values = std::array {4, 6};
            const auto view = carven::runtime::as_slice(values);
            static_assert(std::same_as<decltype(view[0]), const int&>);
            expect(std::addressof(view[1]) == std::addressof(values[1]));
        };
});

} // namespace
