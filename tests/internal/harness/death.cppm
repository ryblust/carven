module carven:test.internal.harness.death;

import std;

using DeathTestAction = void (*)(void*) noexcept;

auto run_death_test(
    std::string_view scenario,
    DeathTestAction action,
    void* context,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(30000)
) noexcept -> bool;

template<typename Function>
auto invoke_death_test_action(void* context) noexcept -> void {
    auto* function = static_cast<Function*>(context);
    static_cast<void>(std::invoke(*function));
}

template<typename Function>
    requires std::invocable<Function&>
auto expect_termination(std::string_view scenario, Function function) noexcept -> bool {
    return run_death_test(scenario, invoke_death_test_action<Function>, std::addressof(function));
}
