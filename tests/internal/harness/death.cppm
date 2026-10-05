module carven:test.internal.harness.death;

import :support.function_ref;
import std;

using DeathTestAction = FunctionRef<void() noexcept>;

// Each input in a test case, including subcases, needs a distinct scenario for child replay.
// Only SIGABRT satisfies the termination contract.
// Validate fixtures before calling; the action contains production operations, not test assertions.
auto run_death_test(
    std::string_view scenario,
    DeathTestAction action,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(30000)
) noexcept -> bool;

template<typename Function>
    requires std::invocable<Function&>
auto expect_termination(std::string_view scenario, Function function) noexcept -> bool {
    return run_death_test(scenario, function);
}
