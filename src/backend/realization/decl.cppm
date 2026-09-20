module carven:backend.realization.decl;

import :backend.target.name;
import :backend.target.stmt;
import std;

auto finish_body_declarations(
    std::vector<TargetStmt>& statements,
    std::span<const TargetLocalID> parameters,
    const std::flat_set<TargetLocalID>& mutable_owners,
    const std::flat_set<TargetLocalID>& removable_locals
) noexcept -> std::vector<bool>;
