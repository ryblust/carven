module carven:backend.realization.decl;

import :backend.target.name;
import :backend.target.stmt;
import std;

// Producers register only storage whose absence preserves lifetime semantics.
enum class UnusedInitializer { Omit, Evaluate };

auto finish_body_declarations(
    std::vector<TargetStmt>& statements,
    std::span<const TargetLocalID> parameters,
    const std::flat_set<TargetLocalID>& mutable_owners,
    const std::flat_map<TargetLocalID, UnusedInitializer>& unused_initializers
) noexcept -> std::vector<bool>;
