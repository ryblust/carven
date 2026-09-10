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

TEST_CASE("Death harness: exceeding the deadline is not expected termination") {
    CHECK_FALSE(run_death_test(
        "death-harness-timeout",
        [](void*) static noexcept {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            std::abort();
        },
        nullptr,
        std::chrono::milliseconds(20)
    ));
}
