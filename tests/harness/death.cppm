module carven:test.harness.death;

import :support.function_ref;
import :test.harness.framework;
import std;

auto termination_test(
    TestFunction body,
    std::source_location location = std::source_location::current()
) noexcept -> TestBody;

[[noreturn]] auto expect_termination(FunctionRef<void() noexcept> action) noexcept -> void;

template<typename Function>
    requires std::is_nothrow_invocable_r_v<void, Function&>
[[noreturn]] auto expect_termination(Function function) noexcept -> void {
    expect_termination(FunctionRef<void() noexcept>(function));
}
