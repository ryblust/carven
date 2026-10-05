module;
#include <carven/runtime/testing.hpp>

module carven:test.internal.runtime.testing;

import :test.harness.framework;
import std;

namespace {

constexpr auto site = carven::runtime::SourceSite::native();

auto reported_cases = std::vector<std::string_view>();

auto record_failure(const carven::runtime::TestFailure& failure) noexcept -> void {
    reported_cases.push_back(failure.case_name);
}

const TestSuite suite([] static noexcept {
    "Runtime: nested test contexts restore the caller and isolate failure state"_test =
        [] static noexcept {
            scenario("inner failure leaves the outer case successful", [] static noexcept {
                auto outer = carven::runtime::TestContext(record_failure);
                auto inner = carven::runtime::TestContext(record_failure);
                outer.begin_case("module", "outer");
                inner.begin_case("module", "inner");
                carven::runtime::report_test_failure(site, "check", "false", std::nullopt, {});
                inner.end_case();
                outer.end_case();
                expect_equal(inner.result(), 1);
                expect_equal(outer.result(), 0);
            });
            scenario("ending the inner case restores reporting to its caller", [] static noexcept {
                auto outer = carven::runtime::TestContext(record_failure);
                auto inner = carven::runtime::TestContext(record_failure);
                outer.begin_case("module", "outer");
                inner.begin_case("module", "inner");
                inner.end_case();
                carven::runtime::report_test_failure(site, "check", "false", std::nullopt, {});
                outer.end_case();
                expect_equal(inner.result(), 0);
                expect_equal(outer.result(), 1);
            });
            expect_range_equal(reported_cases, std::array<std::string_view, 2> {"inner", "outer"});
        };
});

} // namespace
