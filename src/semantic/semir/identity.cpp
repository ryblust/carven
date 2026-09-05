module carven:semantic.semir.identity.impl;

import :semantic.semir.identity;
import :support.invariant;
import std;

auto ProgramIdentity::fresh() noexcept -> ProgramIdentity {
    static auto next_identity = std::atomic<std::uint64_t> {1u};
    const auto value = next_identity.fetch_add(1u, std::memory_order_relaxed);
    if (value == std::numeric_limits<std::uint64_t>::max()) {
        resource_limit_exceeded("program identity space exhausted");
    }
    return ProgramIdentity(value);
}
