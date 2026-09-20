module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>
#ifndef NDEBUG
#define NDEBUG
#endif
#include <carven/runtime/report.hpp>

module carven:test.internal.runtime.assertion;

import :test.internal.harness.death;
import std;

TEST_CASE("Runtime: assertions terminate even with NDEBUG defined") {
    CHECK(expect_termination("assertion-ndebug", []() static noexcept {
        carven::runtime::assertion_failed("source.cv", 7, 3, "assert", "false", "failure", "");
    }));
}
