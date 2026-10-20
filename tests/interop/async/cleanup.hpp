#pragma once

#include <carven/runtime/execution/execution.hpp>
#include <carven/runtime/value/outcome.hpp>

#include "lifetimes/tail_outcomes.hpp"

#include <cstdint>

namespace async_cleanup_probe {

inline bool released = true;

class Lease final {
    tail_outcome_probe::Guard guard;

public:
    Lease() noexcept
        : guard(1) {
        released = false;
    }

    Lease(const Lease&) = delete;
    Lease(Lease&&) = delete;
    auto operator=(const Lease&) -> Lease& = delete;
    auto operator=(Lease&&) -> Lease& = delete;

    ~Lease() { released = true; }
};

class StopOnRelease final {
    Lease lease;

public:
    StopOnRelease() noexcept = default;

    ~StopOnRelease() noexcept {
        tail_outcome_probe::record('s', 5);
        carven::runtime::execution::request_stop();
    }
};

inline auto resource_released() noexcept -> bool {
    return released;
}

inline auto stop_value() noexcept -> std::int32_t {
    tail_outcome_probe::record('s', 7);
    carven::runtime::execution::request_stop();
    return 7;
}

} // namespace async_cleanup_probe

inline auto cleanup_prefix(std::int32_t depth) noexcept -> std::int32_t {
    tail_outcome_probe::record('p', depth);
    if (depth == 2) {
        carven::runtime::execution::request_stop();
    }
    return depth;
}

inline auto cleanup_iteration(std::int32_t value) noexcept -> std::int32_t {
    tail_outcome_probe::record('i', value);
    return value;
}

inline auto cleanup_stopped_value(std::int32_t value) noexcept
    -> carven::runtime::Outcome<std::int32_t> {
    tail_outcome_probe::record('a', value);
    return carven::runtime::Outcome<std::int32_t>::stopped();
}

inline auto cleanup_cancelled() noexcept -> carven::runtime::execution::AsyncOperation<void> {
    co_return carven::runtime::execution::Completion<void>::cancelled();
}

inline auto cleanup_stopped() noexcept -> carven::runtime::execution::AsyncOperation<void> {
    tail_outcome_probe::record('s', 3);
    co_return carven::runtime::execution::Completion<void>::stopped();
}

inline auto cleanup_value() noexcept -> carven::runtime::execution::AsyncOperation<std::int32_t> {
    tail_outcome_probe::record('v', 7);
    co_return carven::runtime::execution::Completion<std::int32_t>::success(7);
}

inline auto cleanup_wait_for_release() noexcept
    -> carven::runtime::execution::AsyncOperation<void> {
    // Native cleanup retains its resource dependency after a root stop.
    for (auto turn = 0; turn < 8; ++turn) {
        if (async_cleanup_probe::resource_released()) {
            tail_outcome_probe::record('c', 2);
            co_return carven::runtime::execution::Completion<void>::success();
        }
        co_await carven::runtime::execution::await_yield_once();
    }
    tail_outcome_probe::record('c', 9);
    co_return carven::runtime::execution::Completion<void>::success();
}

inline auto cleanup_observe_release() noexcept -> carven::runtime::execution::AsyncOperation<void> {
    tail_outcome_probe::record('o', async_cleanup_probe::resource_released() ? 1 : 0);
    co_return carven::runtime::execution::Completion<void>::success();
}

inline auto cleanup_observe_cancellation() noexcept
    -> carven::runtime::execution::AsyncOperation<void> {
    if (carven::runtime::execution::cancel_requested()) {
        tail_outcome_probe::record('c', 4);
        co_return carven::runtime::execution::Completion<void>::success();
    }
    co_await carven::runtime::execution::await_yield_once();
    tail_outcome_probe::record('c', carven::runtime::execution::cancel_requested() ? 4 : 0);
    co_return carven::runtime::execution::Completion<void>::success();
}
