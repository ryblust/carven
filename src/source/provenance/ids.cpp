module carven:source.provenance.ids.impl;

import :source.provenance.ids;
import :support.invariant;
import std;

auto ProvenanceIdentity::fresh() noexcept -> ProvenanceIdentity {
    static auto next_identity = std::atomic<std::uint64_t> {1u};
    const auto value = next_identity.fetch_add(1u, std::memory_order_relaxed);
    if (value == std::numeric_limits<std::uint64_t>::max()) {
        resource_limit_exceeded("provenance identity space exhausted");
    }
    return ProvenanceIdentity(value);
}
