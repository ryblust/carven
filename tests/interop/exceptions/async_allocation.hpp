#pragma once

#include <carven/runtime/execution/execution.hpp>

struct AsyncAllocationPayload final {
    int value;
};

// Separate translation-unit factories keep the returned allocations observable
// across the call boundary. The allocation-failure target disables LTO.
auto async_allocation_operation() noexcept -> carven::runtime::execution::AsyncOperation<void>;
auto async_allocation_success() noexcept
    -> carven::runtime::execution::Completion<AsyncAllocationPayload>;
auto async_allocation_failure() noexcept
    -> carven::runtime::execution::Completion<void, AsyncAllocationPayload>;
