module;
#include <carven/runtime/testing.hpp>

module carven:test.internal.runtime.testing;

import :test.harness.framework;
import std;

namespace {

constexpr auto site = carven::runtime::SourceSite::native();

const TestSuite suite([] static noexcept {
    "Runtime: nested test contexts restore the caller and isolate failure state"_test =
        [] static noexcept {
            auto outer = carven::runtime::TestContext();
            auto inner = carven::runtime::TestContext(
                +[](const carven::runtime::TestFailure&) static noexcept {}
            );
            outer.begin_case("module", "outer");
            expect(std::addressof(carven::runtime::current_test(site)) == std::addressof(outer));
            if (!expect(carven::runtime::active_test_report != nullptr)) {
                return;
            }
            expect(carven::runtime::active_test_report->case_name == "outer");
            inner.begin_case("module", "inner");
            expect(carven::runtime::active_test_report->case_name == "inner");
            expect(std::addressof(carven::runtime::current_test(site)) == std::addressof(inner));
            carven::runtime::current_test(site)
                .report_failure({"test.cv", 1, 1}, "check", "false", std::nullopt, {});
            inner.end_case();
            expect(inner.result() == 1);
            expect(std::addressof(carven::runtime::current_test(site)) == std::addressof(outer));
            expect(carven::runtime::active_test_report->case_name == "outer");
            outer.end_case();
            expect(carven::runtime::active_test_report == nullptr);
            expect(outer.result() == 0);
        };
});

} // namespace
