module;
#include <carven/runtime/testing.hpp>

module carven:test.internal.runtime.testing;

import :test.harness.framework;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test(
        "Runtime: nested test contexts restore the caller and isolate failure state",
        [] static noexcept {
            auto outer = carven::runtime::TestContext();
            auto inner = carven::runtime::TestContext(
                +[](const carven::runtime::TestFailure&) static noexcept {}
            );
            outer.begin_case("module", "outer");
            ct::expect(std::addressof(carven::runtime::current_test()) == std::addressof(outer));
            if (!ct::expect(carven::runtime::active_test_report != nullptr)) {
                return;
            }
            ct::expect(carven::runtime::active_test_report->case_name == "outer");
            inner.begin_case("module", "inner");
            ct::expect(carven::runtime::active_test_report->case_name == "inner");
            ct::expect(std::addressof(carven::runtime::current_test()) == std::addressof(inner));
            carven::runtime::current_test()
                .report_failure("test.cv", 1, 1, "check", "false", std::nullopt, {});
            inner.end_case();
            ct::expect(inner.result() == 1);
            ct::expect(std::addressof(carven::runtime::current_test()) == std::addressof(outer));
            ct::expect(carven::runtime::active_test_report->case_name == "outer");
            outer.end_case();
            ct::expect(carven::runtime::active_test_report == nullptr);
            ct::expect(outer.result() == 0);
        }
    );
});

} // namespace
