#include "mode.hpp"

#include <cstdio>
#include <cstdlib>

namespace {

namespace async = carven::runtime::async;

auto caller() noexcept -> async::Operation<int> {
    auto& activation = co_await async::current_activation();
    auto scope = async::ChildScope(activation);
    auto child = scope.start(async_mode_operation(40));
    auto result = co_await child.observe();
    co_await scope.close(async::ClosingPolicy::CloseOnly);
    if (result.success_if() == nullptr) {
        std::exit(EXIT_FAILURE);
    }
    co_return co_await async_mode_operation(result.success_if()->value);
}

} // namespace

auto main() -> int {
    const auto layout = async_mode_layout();
#if defined(NDEBUG)
    constexpr auto debug = false;
#else
    constexpr auto debug = true;
#endif
    if (layout.debug == debug
        || layout.work_node != sizeof(async::detail::WorkNode)
        || layout.state != sizeof(async::OperationState)
        || layout.promise != sizeof(async::Operation<int>::promise_type)
        || layout.child_scope != sizeof(async::ChildScope)) {
        return EXIT_FAILURE;
    }
    const auto result = async::drive_root(caller());
    if (result.success_if() == nullptr || result.success_if()->value != 42) {
        return EXIT_FAILURE;
    }
    std::puts("async mixed mode passed");
    return EXIT_SUCCESS;
}
