module carven:backend.target.stmt.impl;

import :backend.target.stmt;
import std;

auto discard_statement(TargetExpr expression) noexcept -> TargetStmt {
    return {
        .value = TargetDiscardStmt {.expression = std::move(expression)},
        .attribution =
            TargetGeneratedExpansionAttribution {.reason = TargetExpansionReason::LoweringSupport}
    };
}
