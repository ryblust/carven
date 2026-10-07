#pragma once

#include "../lifetimes/tail_outcomes.hpp"
#include <carven/runtime/async/async.hpp>

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

inline auto resource_released() noexcept -> bool {
    return released;
}

} // namespace async_cleanup_probe

inline auto cleanup_cancelled() noexcept -> carven::runtime::async::Operation<void> {
    co_return carven::runtime::async::Completion<void>::cancelled();
}
