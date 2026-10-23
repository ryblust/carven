module;

#include <carven/hosting/request.hpp>

module carven:hosting.request.impl;

import :support.invariant;
import std;

extern "C++" {

HostRequestState::HostRequestState(void* context) noexcept
    : mutex(),
      id(0),
      phase(Phase::Pending),
      context(context),
      cancel_hook(nullptr) {
    static auto next = std::atomic<std::uint64_t>(1);
    id = next.fetch_add(1, std::memory_order_relaxed);
    if (id == 0 || id == std::numeric_limits<std::uint64_t>::max()) {
        invariant_violation("hosting request identities exhausted");
    }
}

auto HostRequestState::cancel() noexcept -> void {
    void (*hook)(void*, std::uint64_t) noexcept = nullptr;
    void* recipient = nullptr;
    {
        const auto lock = std::lock_guard(mutex);
        if (phase == Phase::Consumed || phase == Phase::Cancelled) {
            return;
        }
        phase = Phase::Cancelled;
        discard();
        recipient = std::exchange(context, nullptr);
        hook = std::exchange(cancel_hook, nullptr);
    }
    if (hook != nullptr) {
        hook(recipient, id);
    }
}

auto HostRequestState::attached() noexcept -> bool {
    const auto lock = std::lock_guard(mutex);
    return phase == Phase::Pending || phase == Phase::Ready;
}

auto hosting_request_id(const std::shared_ptr<HostRequestState>& state) noexcept -> std::uint64_t {
    return state ? state->id : 0;
}

auto hosting_request_active(const std::shared_ptr<HostRequestState>& state) noexcept -> bool {
    if (!state) {
        return false;
    }
    const auto lock = std::lock_guard(state->mutex);
    return state->phase == HostRequestState::Phase::Pending;
}

} // extern "C++"
