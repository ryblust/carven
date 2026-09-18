module carven:backend.realization.decl;

import :backend.target.name;
import :backend.target.stmt;
import std;

auto finish_body_declarations(
    std::span<TargetStmt> statements,
    std::span<const TargetLocalID> parameters,
    const std::flat_set<TargetLocalID>& mutable_owners
) noexcept -> std::vector<bool>;
