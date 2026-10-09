#include "mode.hpp"

namespace async = carven::runtime::execution;

namespace {

auto leaf(int value) noexcept -> async::AsyncOperation<int> {
    co_await async::yield_once();
    co_return async::Completion<int>::success(value + 1);
}

} // namespace

auto async_mode_layout() noexcept -> AsyncModeLayout {
    return {
#if defined(NDEBUG)
        .debug = false,
#else
        .debug = true,
#endif
        .work_node = sizeof(async::detail::WorkNode),
        .state = sizeof(async::OperationState),
        .promise = sizeof(async::AsyncOperation<int>::promise_type),
        .child_scope = sizeof(async::ChildScope),
    };
}

auto async_mode_operation(int value) noexcept -> async::AsyncOperation<int> {
    auto& activation = co_await async::current_activation();
    auto scope = async::ChildScope(activation);
    auto child = scope.start(leaf(value));
    auto result = co_await child.observe();
    co_await scope.close(async::ClosingPolicy::CloseOnly);
    co_return std::move(result);
}
