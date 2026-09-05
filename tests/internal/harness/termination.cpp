module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.harness.termination;

import :test.internal.harness.death;
import std;

TEST_CASE("Death harness: abnormal and ordinary completion remain distinct") {
    CHECK(expect_termination("death-harness-abnormal-exit", []() static noexcept {
        std::abort();
    }));
    CHECK_FALSE(expect_termination("death-harness-ordinary-return", []() static noexcept {}));
}
