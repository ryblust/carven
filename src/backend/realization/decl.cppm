module carven:backend.realization.decl;

import :backend.target.name;
import :backend.target.stmt;
import std;

auto finish_body_declarations(
    std::span<TargetStmt> statements,
    std::span<const TargetIdentifier> parameters,
    std::span<const TargetIdentifier> captures
) noexcept -> std::vector<bool>;
