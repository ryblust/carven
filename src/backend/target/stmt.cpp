module carven:backend.target.stmt.impl;

import :backend.target.stmt;
import std;

auto statement_expression(TargetExpr expression) noexcept -> TargetStmt {
    return {
        .value = TargetExprStmt {.expression = std::move(expression)},
        .attribution =
            TargetGeneratedExpansionAttribution {.reason = TargetExpansionReason::LoweringSupport}
    };
}
