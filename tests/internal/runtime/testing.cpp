module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>
#include <carven/runtime/testing.hpp>

module carven:test.internal.runtime.testing;

import std;

TEST_CASE("Runtime: nested test contexts restore the caller and isolate failure state") {
    auto outer = carven::runtime::TestContext();
    auto inner =
        carven::runtime::TestContext(+[](const carven::runtime::TestFailure&) static noexcept {});
    outer.begin_case("module", "outer");
    CHECK(std::addressof(carven::runtime::current_test()) == std::addressof(outer));
    REQUIRE(carven::runtime::active_test_report != nullptr);
    CHECK(carven::runtime::active_test_report->case_name == "outer");
    inner.begin_case("module", "inner");
    CHECK(carven::runtime::active_test_report->case_name == "inner");
    CHECK(std::addressof(carven::runtime::current_test()) == std::addressof(inner));
    carven::runtime::current_test()
        .report_failure("test.cv", 1, 1, "check", "false", std::nullopt, {});
    inner.end_case();
    CHECK(inner.result() == 1);
    CHECK(std::addressof(carven::runtime::current_test()) == std::addressof(outer));
    CHECK(carven::runtime::active_test_report->case_name == "outer");
    outer.end_case();
    CHECK(carven::runtime::active_test_report == nullptr);
    CHECK(outer.result() == 0);
}
