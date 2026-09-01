module carven:backend.lowering.match;

import :backend.lowering.program;
import :semantic.hir.expr;
import :semantic.hir.ids;
import :semantic.hir.stmt;
import std;

auto lower_statement_match(
    TargetCallableLowerer& context,
    const HIRMatchStmt& match,
    const TargetControlDestinations& control
) noexcept -> std::vector<TargetStmtID>;

auto lower_value_match(
    TargetCallableLowerer& context,
    const HIRMatchExpr& match,
    const TargetControlDestinations& control
) noexcept -> std::vector<TargetStmtID>;

auto lower_outcome_match(
    TargetCallableLowerer& context,
    const HIRMatchExpr& match,
    const TargetControlDestinations& control,
    HIRTypeID result_type_id,
    FailureSetID failure_set_id
) noexcept -> std::vector<TargetStmtID>;
