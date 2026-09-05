module carven:test.internal.harness.death;

import std;

namespace death_test_detail {

using Action = void (*)(void*) noexcept;

auto expect_termination_impl(std::string_view scenario, Action action, void* context) noexcept
    -> bool;

template<typename Function>
auto invoke_action(void* context) noexcept -> void {
    auto* function = static_cast<Function*>(context);
    static_cast<void>(std::invoke(*function));
}

} // namespace death_test_detail

template<typename Function>
    requires std::invocable<Function&>
auto expect_termination(std::string_view scenario, Function function) noexcept -> bool {
    return death_test_detail::expect_termination_impl(
        scenario,
        death_test_detail::invoke_action<Function>,
        std::addressof(function)
    );
}
