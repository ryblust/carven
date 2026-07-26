module carven:backend.lowering.statements;

import :backend.lowering.program;
import :backend.target.ids;
import :backend.target.stmt;
import :semantic.hir.expr;
import :semantic.hir.ids;
import :semantic.hir.stmt;
import std;

auto lower_block(
    TargetCallableLowerer& context,
    HIRBlockID id,
    const TargetControlDestinations& control,
    bool return_result = false
) noexcept -> std::vector<TargetStmtID>;
auto lower_statements(
    TargetCallableLowerer& context,
    std::span<const HIRStmtID> statements,
    const TargetControlDestinations& control
) noexcept -> std::vector<TargetStmtID>;
auto lower_outcome_block(
    TargetCallableLowerer& context,
    HIRBlockID id,
    HIRTypeID result,
    FailureSetID failure_set,
    const TargetControlDestinations& control
) noexcept -> std::vector<TargetStmtID>;
auto lower_statement(
    TargetCallableLowerer& context,
    HIRStmtID id,
    const TargetControlDestinations& control
) noexcept -> TargetStmtID;
auto lower_statement_value(
    TargetCallableLowerer& context,
    const HIRStmt& source,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue;
auto with_prelude(
    TargetCallableLowerer& context,
    std::vector<TargetStmtID> prelude,
    TargetStmtValue value
) noexcept -> TargetStmtValue;
auto lower_statement(
    TargetCallableLowerer& context,
    const HIRReturnStmt& statement,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue;
auto lower_statement(
    TargetCallableLowerer& context,
    const HIRThrowStmt& statement,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue;
auto lower_statement(
    TargetCallableLowerer& context,
    const HIRRethrowStmt& statement,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue;
auto lower_statement(
    TargetCallableLowerer& context,
    const HIRExprStmt& statement,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue;
auto lower_statement(
    TargetCallableLowerer& context,
    const HIRBindingStmt& statement,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue;
auto lower_statement(
    TargetCallableLowerer& context,
    const HIRBreakStmt& statement,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue;
auto lower_statement(
    TargetCallableLowerer& context,
    const HIRContinueStmt& statement,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue;
auto lower_statement(
    TargetCallableLowerer& context,
    const HIRAssignmentStmt& statement,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue;
auto lower_statement(
    TargetCallableLowerer& context,
    const HIRUpdateStmt& statement,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue;
auto lower_statement(
    TargetCallableLowerer& context,
    const HIRIfStmt& statement,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue;
auto lower_statement(
    TargetCallableLowerer& context,
    const HIRMatchStmt& statement,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue;
auto lower_statement(
    TargetCallableLowerer& context,
    const HIRWhileStmt& statement,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue;
auto lower_statement(
    TargetCallableLowerer& context,
    const HIRCStyleForStmt& statement,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue;
auto lower_statement(
    TargetCallableLowerer& context,
    const HIRRangeForStmt& statement,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue;
auto lower_statement(
    TargetCallableLowerer& context,
    const HIRTestCheckStmt& statement,
    ProgramOriginID origin,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue;
auto lower_statement(
    TargetCallableLowerer& context,
    const HIRTestRequireStmt& statement,
    ProgramOriginID origin,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue;
auto lower_statement(
    TargetCallableLowerer& context,
    const HIRTestFailStmt& statement,
    ProgramOriginID origin,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue;
auto lower_statement(const TargetCallableLowerer& context, const HIRCppStmt& statement) noexcept
    -> TargetStmtValue;
auto lower_void_try(
    TargetCallableLowerer& context,
    HIRExprID id,
    const HIRTryExpr& expression,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue;
