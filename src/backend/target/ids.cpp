module carven:backend.target.ids.impl;

import :backend.target.ids;
import :support.invariant;
import std;

auto TargetPlanIdentity::fresh() noexcept -> TargetPlanIdentity {
    static auto next = std::atomic<std::uint64_t>(1u);
    const auto value = next.fetch_add(1u, std::memory_order_relaxed);
    if (value == std::numeric_limits<std::uint64_t>::max()) {
        resource_limit_exceeded("target plan identity space is exhausted");
    }
    return TargetPlanIdentity(value);
}

auto TargetUnitIdentity::fresh() noexcept -> TargetUnitIdentity {
    static auto next = std::atomic<std::uint64_t>(1u);
    const auto value = next.fetch_add(1u, std::memory_order_relaxed);
    if (value == std::numeric_limits<std::uint64_t>::max()) {
        resource_limit_exceeded("target unit identity space is exhausted");
    }
    return TargetUnitIdentity(value);
}
