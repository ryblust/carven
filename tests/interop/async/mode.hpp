#pragma once

#include <carven/runtime/execution/execution.hpp>
#include <cstddef>

struct AsyncModeLayout final {
    bool debug;
    std::size_t work_node;
    std::size_t state;
    std::size_t promise;
    std::size_t child_scope;
};

auto async_mode_layout() noexcept -> AsyncModeLayout;
auto async_mode_operation(int value) noexcept -> carven::runtime::execution::Operation<int>;
