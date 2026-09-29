module;
#include <csignal>

module carven:test.internal.harness.termination;

import :test.harness.framework;
import :test.internal.harness.death;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite death_tests([] static noexcept {
    ct::test(
        "Death harness: repeated scenario names cannot replay a different action",
        [] static noexcept {
            ct::expect(expect_termination("duplicate-scenario", [] static noexcept {
                std::abort();
            }));
            ct::expect(!expect_termination("duplicate-scenario", [] static noexcept {
                std::abort();
            }));
        }
    );
    ct::test("Death harness: abort satisfies the termination contract", [] static noexcept {
        ct::expect(expect_termination("death-harness-abnormal-exit", []() static noexcept {
            std::abort();
        }));
    });

    ct::test(
        "Death harness: ordinary completion and other failures do not satisfy the contract",
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
            ct::each(scenarios, &Scenario::name, [](const Scenario& scenario) static noexcept {
                ct::expect(!expect_termination(scenario.name, scenario.action));
            });
        }
    );

    ct::test(
        "Death harness: exceeding the deadline is not expected termination",
        [] static noexcept {
            ct::expect(!run_death_test(
                "death-harness-timeout",
                [](void*) static noexcept {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    std::abort();
                },
                nullptr,
                std::chrono::milliseconds(20)
            ));
        }
    );
});

} // namespace
