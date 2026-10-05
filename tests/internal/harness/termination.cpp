module;
#include <csignal>

module carven:test.internal.harness.termination;

import :test.harness.framework;
import :test.internal.harness.death;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Death harness: repeated scenario names cannot replay a different action"_test =
        [] static noexcept {
            expect(expect_termination("duplicate-scenario", [] static noexcept { std::abort(); }));
            expect(!expect_termination("duplicate-scenario", [] static noexcept { std::abort(); }));
        };
    "Death harness: abort satisfies the termination contract"_test = [] static noexcept {
        expect(expect_termination("death-harness-abnormal-exit", []() static noexcept {
            std::abort();
        }));
    };

    "Death harness: ordinary completion and other failures do not satisfy the contract"_test =
        [] static noexcept {
            struct Scenario final {
                std::string_view name;
                void (*action)() noexcept;
            };

            const auto scenarios = std::array {
                Scenario {.name = "return", .action = []() static noexcept {}},
                Scenario {.name = "exit", .action = []() static noexcept { std::_Exit(1); }},
                Scenario {
                    .name = "reserved-exit",
                    .action = []() static noexcept { std::_Exit(73); }
                },
                Scenario {
                    .name = "signal",
                    .action = []() static noexcept { static_cast<void>(std::raise(SIGTERM)); },
                },
            };
            each(scenarios, &Scenario::name, [](const Scenario& scenario) static noexcept {
                expect(!expect_termination(scenario.name, scenario.action));
            });
        };

    "Death harness: exceeding the deadline is not expected termination"_test = [] static noexcept {
        expect(!run_death_test(
            "death-harness-timeout",
            [](void*) static noexcept {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                std::abort();
            },
            nullptr,
            std::chrono::milliseconds(20)
        ));
    };
});

} // namespace
