module carven:backend.lowering.stmt;

import :backend.lowering.program;
import :backend.target.ids;
import :backend.target.origin;
import :backend.target.stmt;
import :semantic.hir.expr;
import :semantic.hir.ids;
import :semantic.hir.stmt;
import std;

class PreparedTargetStatement final {
public:
    explicit PreparedTargetStatement(TargetStmt statement) noexcept;
    PreparedTargetStatement(const PreparedTargetStatement&) = delete;
    PreparedTargetStatement(PreparedTargetStatement&& other) noexcept;
    auto operator=(const PreparedTargetStatement&) -> PreparedTargetStatement& = delete;
    auto operator=(PreparedTargetStatement&&) -> PreparedTargetStatement& = delete;
    ~PreparedTargetStatement() noexcept;

    auto classify_for_initializer() && noexcept -> PreparedTargetStatement;
    auto classify_for_step() && noexcept -> PreparedTargetStatement;
    auto is_for_initializer() const noexcept -> bool;
    auto is_for_step() const noexcept -> bool;
    auto take_for_initializer() && noexcept -> TargetForInitializer;
    auto take_for_step() && noexcept -> TargetForStep;
    auto publish(TargetCallableLowerer& context) && noexcept -> TargetStmtID;

private:
    struct ClassifiedFallback final {
        TargetStmt statement;
    };

    struct ClassifiedForInitializer final {
        TargetForInitializer initializer;
        TargetAttribution attribution;
    };

    struct ClassifiedForStep final {
        TargetForStep step;
        TargetAttribution attribution;
    };

    using State =
        std::variant<TargetStmt, ClassifiedFallback, ClassifiedForInitializer, ClassifiedForStep>;

    explicit PreparedTargetStatement(State state) noexcept;
    auto take_state() noexcept -> State;

    std::optional<State> state;
};

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
    HIRStmtID statement_id,
    const TargetControlDestinations& control
) noexcept -> TargetStmtID;

auto prepare_target_statement(
    TargetCallableLowerer& context,
    HIRStmtID statement_id,
    const TargetControlDestinations& control
) noexcept -> PreparedTargetStatement;

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
