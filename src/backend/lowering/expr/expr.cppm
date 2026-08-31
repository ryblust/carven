module carven:backend.lowering.expr;

import :backend.lowering.program;
import :backend.target.expr;
import :backend.target.ids;
import :backend.target.name;
import :backend.target.stmt;
import :backend.target.symbol;
import :semantic.hir.expr;
import :semantic.hir.ids;
import :semantic.hir.place;
import std;

struct LoweredExpression final {
    std::vector<TargetStmtID> prelude;
    TargetExprID expression;
    std::optional<FailureCarrierDescriptor> unconsumed_carrier = std::nullopt;
};

struct MaterializedExpressionName final {
    std::vector<TargetStmtID> prelude_ids;
    TargetIdentifier name;
    std::optional<FailureCarrierDescriptor> unconsumed_carrier;
};

enum class CarrierInputCategory {
    Rvalue,
    Lvalue,
};

struct LoweredCatchHandlers final {
    std::optional<TargetIdentifier> matched_name;
    std::vector<TargetIfBranch> exclusive_branches;
    std::vector<TargetStmtID> statements;
};

enum class MaterializationKind {
    ReadValue,
    ReadReference,
    WriteReference,
    Take,
    Preserve,
    Snapshot,
};

auto name_expression(TargetModuleLowerer& context, TargetName name) noexcept -> TargetExprID;

auto name_expression(TargetModuleLowerer& context, TargetSymbol symbol) noexcept -> TargetExprID;

auto call_expression(
    TargetModuleLowerer& context,
    TargetExprID callee,
    std::vector<TargetExprID> arguments
) noexcept -> TargetExprID;

auto lower_constant_value(TargetCallableLowerer& context, HIRConstantID constant) noexcept
    -> TargetExprID;

class TargetEvaluationSequencer final {
public:
    static auto materialize(
        TargetCallableLowerer& context,
        LoweredExpression expression,
        MaterializationKind materialization,
        TargetMaterializationReason reason
    ) noexcept -> LoweredExpression;

    static auto materialize_read_name(
        TargetCallableLowerer& context,
        LoweredExpression expression,
        HIRTypeID type_id,
        TargetMaterializationReason reason
    ) noexcept -> MaterializedExpressionName;

    static auto read_materialization(const TargetCallableLowerer& context, HIRTypeID type) noexcept
        -> MaterializationKind;

    static auto effects_commute(
        const EvaluationEffect& left,
        bool left_may_terminate,
        const EvaluationEffect& right,
        bool right_may_terminate
    ) noexcept -> bool;

    static auto expressions_commute(
        const TargetCallableLowerer& context,
        HIRExprID left,
        HIRExprID right
    ) noexcept -> bool;

    static auto stable_place_source(
        const TargetCallableLowerer& context,
        HIRExprID expression
    ) noexcept -> bool;
};

auto cpp_bool_cast(TargetCallableLowerer& context, TargetExprID expression) noexcept
    -> TargetExprID;

auto static_member_expression(
    TargetModuleLowerer& context,
    TargetTypeID owner,
    TargetIdentifier name
) noexcept -> TargetExprID;

auto member_call_expression(
    TargetModuleLowerer& context,
    TargetExprID operand_id,
    TargetIdentifier name,
    std::vector<TargetTypeID> template_argument_type_ids = {},
    std::vector<TargetExprID> argument_ids = {}
) noexcept -> TargetExprID;

auto outcome_success_expression(
    TargetCallableLowerer& context,
    TargetTypeID carrier,
    std::optional<TargetExprID> value = std::nullopt
) noexcept -> TargetExprID;

auto outcome_failure_expression(
    TargetCallableLowerer& context,
    TargetTypeID carrier,
    TargetExprID value
) noexcept -> TargetExprID;

auto take_outcome_failure(
    TargetCallableLowerer& context,
    TargetExprID outcome,
    TargetTypeID failure
) noexcept -> TargetExprID;

auto consume_failure_carrier(
    TargetCallableLowerer& context,
    LoweredExpression outcome,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression;

auto convert_carrier_expression(
    TargetCallableLowerer& context,
    TargetExprID expression,
    const FailureCarrierDescriptor& source,
    const FailureCarrierDescriptor& destination,
    CarrierInputCategory input_category
) noexcept -> TargetExprID;

auto lower_unhandled_failure_transfer(
    TargetCallableLowerer& context,
    const TargetIdentifier& outcome_name,
    FailureSetID failure_set,
    const TargetControlDestinations& control
) noexcept -> std::vector<TargetStmtID>;

auto lower_catch_handlers(
    TargetCallableLowerer& context,
    std::span<const HIRCatchArm> arms,
    std::span<const HIRCatchFacts> facts,
    const FailureCarrierDescriptor& carrier,
    TargetIdentifier carrier_name,
    const TargetControlDestinations& control,
    std::optional<HIRTypeID> result = std::nullopt,
    std::optional<FailureSetID> failure_set = std::nullopt
) noexcept -> LoweredCatchHandlers;

auto complete_catch_handlers(
    TargetCallableLowerer& context,
    LoweredCatchHandlers handlers,
    std::vector<TargetStmtID> unmatched
) noexcept -> std::vector<TargetStmtID>;

auto test_success_expression(
    TargetCallableLowerer& context,
    TargetTypeID result,
    std::optional<TargetExprID> value = std::nullopt
) noexcept -> TargetExprID;

auto test_exit_statement(
    TargetCallableLowerer& context,
    const TargetControlDestinations& control
) noexcept -> TargetStmtID;

auto propagated_test_value(
    TargetCallableLowerer& context,
    TargetExprID value,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression;

auto carrier_failure_set(const TargetCallableLowerer& context, HIRExprID expression) noexcept
    -> FailureSetID;

auto call_result_carrier(TargetCallableLowerer& context, HIRExprID call, HIRExprID callee) noexcept
    -> std::optional<FailureCarrierDescriptor>;

auto ordered_call(
    TargetCallableLowerer& context,
    LoweredExpression callee,
    std::span<const HIRCallArgument> arguments,
    bool foreign_callee,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression;

auto lower_expression(
    TargetCallableLowerer& context,
    HIRExprID id,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression;

auto lower_unconsumed_expression(
    TargetCallableLowerer& context,
    HIRExprID id,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression;

auto lower_tail_expression(
    TargetCallableLowerer& context,
    HIRExprID id,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression;

auto convert_tail_carrier(
    TargetCallableLowerer& context,
    LoweredExpression expression,
    const FailureCarrierDescriptor& destination
) noexcept -> LoweredExpression;

auto lower_expression(
    TargetCallableLowerer& context,
    HIRExprID id,
    const HIRLiteralExpr& expression,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression;

auto lower_expression(
    TargetCallableLowerer& context,
    HIRExprID id,
    const HIRNameExpr& expression,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression;

auto lower_expression(
    TargetCallableLowerer& context,
    HIRExprID id,
    const HIRArrayExpr& expression,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression;

auto lower_expression(
    TargetCallableLowerer& context,
    HIRExprID id,
    const HIRConstructionExpr& expression,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression;

auto lower_expression(
    TargetCallableLowerer& context,
    HIRExprID id,
    const HIRCaseConstructionExpr& expression,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression;

auto lower_expression(
    TargetCallableLowerer& context,
    HIRExprID id,
    const HIRUnaryExpr& expression,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression;

auto lower_expression(
    TargetCallableLowerer& context,
    HIRExprID id,
    const HIRBinaryExpr& expression,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression;

auto lower_expression(
    TargetCallableLowerer& context,
    HIRExprID id,
    const HIRCastExpr& expression,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression;

auto lower_expression(
    TargetCallableLowerer& context,
    HIRExprID id,
    const HIRTakeExpr& expression,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression;

auto lower_expression(
    TargetCallableLowerer& context,
    HIRExprID id,
    const HIRCallExpr& expression,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression;

auto lower_expression(
    TargetCallableLowerer& context,
    HIRExprID id,
    const HIRClosureExpr& expression,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression;

auto lower_expression(
    TargetCallableLowerer& context,
    HIRExprID id,
    const HIRCallableViewExpr& expression,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression;

auto lower_expression(
    TargetCallableLowerer& context,
    HIRExprID id,
    const HIRPropagationExpr& expression,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression;

auto lower_expression(
    TargetCallableLowerer& context,
    HIRExprID id,
    const HIRTextIntrinsicExpr& expression,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression;

auto lower_expression(
    TargetCallableLowerer& context,
    HIRExprID id,
    const HIRIndexExpr& expression,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression;

auto lower_expression(
    TargetCallableLowerer& context,
    HIRExprID id,
    const HIRMemberExpr& expression,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression;

auto lower_expression(
    TargetCallableLowerer& context,
    HIRExprID id,
    const HIRCppExpr& expression
) noexcept -> LoweredExpression;

auto lower_expression(
    TargetCallableLowerer& context,
    HIRExprID id,
    const HIRIfExpr& expression,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression;

auto lower_expression(
    TargetCallableLowerer& context,
    HIRExprID id,
    const HIRMatchExpr& expression,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression;

auto lower_expression(
    TargetCallableLowerer& context,
    HIRExprID id,
    const HIRTryExpr& expression,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression;

auto lower_mutation_expression(
    TargetCallableLowerer& context,
    HIRExprID id,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression;
