#pragma once

#include <carven/runtime/execution/execution.hpp>

#include <cstdint>

inline auto carven_time_now_nanoseconds() noexcept -> std::int64_t {
    return carven::runtime::execution::now();
}

inline auto carven_time_wait_until_nanoseconds(std::int64_t deadline_nanoseconds) noexcept
    -> carven::runtime::execution::AsyncOperation<void> {
    return carven::runtime::execution::sleep_until(deadline_nanoseconds);
}
