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
    inner.begin_case("module", "inner");
    CHECK(std::addressof(carven::runtime::current_test()) == std::addressof(inner));
    carven::runtime::current_test().report_failure("test.cv", 1, "check", "false", std::nullopt);
    inner.end_case();
    CHECK(inner.result() == 1);
    CHECK(std::addressof(carven::runtime::current_test()) == std::addressof(outer));
    outer.end_case();
    CHECK(outer.result() == 0);
}
