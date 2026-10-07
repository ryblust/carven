#if defined(_MSC_VER)
#undef _HAS_EXCEPTIONS
#define _HAS_EXCEPTIONS 1
#endif

#include "async_allocation.hpp"

namespace async = carven::runtime::async;

auto async_allocation_operation() noexcept -> async::Operation<void> {
    co_return async::Completion<void>::success();
}

auto async_allocation_success() noexcept -> async::Completion<AsyncAllocationPayload> {
    return async::Completion<AsyncAllocationPayload>::success_from([]() noexcept {
        return AsyncAllocationPayload {.value = 7};
    });
}

auto async_allocation_failure() noexcept -> async::Completion<void, AsyncAllocationPayload> {
    return async::Completion<void, AsyncAllocationPayload>::failure(
        AsyncAllocationPayload {.value = 7}
    );
}
