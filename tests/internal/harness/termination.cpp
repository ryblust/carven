module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>
#include <csignal>

module carven:test.internal.harness.termination;

import :test.internal.harness.death;
import std;

TEST_CASE("Death harness: abort satisfies the termination contract") {
    CHECK(expect_termination("death-harness-abnormal-exit", []() static noexcept {
        std::abort();
    }));
}

TEST_CASE("Death harness: ordinary completion and other failures do not satisfy the contract") {
    struct Scenario final {
        std::string_view name;
        void (*action)() noexcept;
    };

    const auto scenarios = std::array {
        Scenario {.name = "return", .action = []() static noexcept {}},
        Scenario {.name = "exit", .action = []() static noexcept { std::_Exit(1); }},
        Scenario {.name = "reserved-exit", .action = []() static noexcept { std::_Exit(73); }},
        Scenario {
            .name = "signal",
            .action = []() static noexcept { static_cast<void>(std::raise(SIGTERM)); },
        },
    };
    for (const auto& scenario : scenarios) {
        CAPTURE(scenario.name);
        CHECK_FALSE(expect_termination(scenario.name, scenario.action));
    }
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
