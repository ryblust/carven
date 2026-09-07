module carven:backend.lowering.body.lowerer;

import :backend.generation.names;
import :backend.generation.plan;
import :backend.lowering.body;
import :backend.lowering.body.composition;
import :backend.lowering.context;
import :backend.target.expr;
import :backend.target.ids;
import :backend.target.name;
import :backend.target.origin;
import :backend.target.stmt;
import :semantic.semir;
import std;

auto binary_expression(TargetExpr left, TargetBinaryOperator operation, TargetExpr right) noexcept
    -> TargetExpr;

auto prefix_expression(TargetPrefixOperator operation, TargetExpr operand) noexcept -> TargetExpr;

auto template_call_expression(
    TargetExpr callee,
    std::vector<TargetTypeID> template_arguments,
    std::vector<TargetExpr> arguments
) noexcept -> TargetExpr;

auto call_member(
    TargetExpr owner,
    std::string_view member,
    std::vector<TargetExpr> arguments
) noexcept -> TargetExpr;

auto statement_expression(TargetExpr expression) noexcept -> TargetStmt;

enum class LoweringLiteralContext { Exact, TargetTyped };

auto constant_expression(
    ModuleLowering& context,
    ConstantID constant,
    LoweringLiteralContext use = LoweringLiteralContext::Exact
) noexcept -> TargetExpr;

auto typed_integer_expression(
    ModuleLowering& context,
    const IntegerConstant& value,
    TypeID type,
    LoweringLiteralContext use = LoweringLiteralContext::Exact
) noexcept -> TargetExpr;

auto enum_case_index(const SemIRProgram& semantic, EnumCaseID case_id) noexcept -> std::size_t;

auto enum_case_expression(
    ModuleLowering& context,
    EnumCaseID case_id,
    std::vector<TargetExpr> payload
) noexcept -> TargetExpr;

class BodyLowerer final {
public:
    BodyLowerer(ModuleLowering& source_context, BodyID id, TargetBodyInputs target_inputs) noexcept;

    auto finish() noexcept -> LoweredBody;

private:
    static constexpr auto callable_scope = TargetScopeID {.ordinal = 0};

    struct PatternPayloadStep final {
        EnumCaseID enum_case;
        std::uint32_t payload_index;
        std::optional<TargetIdentifier> projection;
    };

    struct PatternSubject final {
        TargetIdentifier root;
        bool dereference_root;
        std::vector<PatternPayloadStep> payload_path;
    };

    struct PatternConstraint final {
        PatternSubject subject;
        std::variant<ConstantID, EnumCaseID> value;
        std::optional<TargetIdentifier> projection;
    };

    struct PatternBinding final {
        LocalBindingID binding;
        PatternSubject subject;
    };

    struct PatternSelection final {
        std::vector<PatternConstraint> constraints;
        std::vector<PatternBinding> bindings;
        std::optional<TargetIdentifier> failure_projection;
    };

    struct PatternProjection final {
        PatternSubject subject;
        EnumCaseID enum_case;
        TargetIdentifier name;
    };

    struct FailureDestination final {
        TargetIdentifier storage;
        TargetIdentifier label;
        LoweringExitTarget target;
    };

    struct CaughtFailure final {
        TargetIdentifier storage;
        FailureSetID failures;
    };

    struct RegionExit final {
        TargetIdentifier label;
        LoweringExitTarget target;
    };

    auto lower_arm(
        std::vector<PatternSelection> selections,
        std::span<const LocalBindingID> bindings,
        const SemanticRegion& source,
        const std::optional<SemanticExpression>& guard,
        const LoweringResultDestination& result,
        RegionExit& done
    ) noexcept -> LoweringStmtBuilder;

    auto retain_evaluation(const SemanticExpression& source) noexcept -> Lowered<LoweringUnit>;
    auto read_value(
        Lowered<LoweringValue> evaluation,
        LoweringStmtBuilder& destination,
        LoweringValueUse use = LoweringValueUse::Transfer
    ) noexcept -> std::optional<TargetExpr>;
    enum class ResultDemand { Value, Observe, Discard };
    auto construct_operation(
        const SemanticExpression& source,
        std::vector<TargetExpr> operands,
        ResultDemand demand
    ) noexcept -> Lowered<LoweringValue>;
    auto consume_expression(
        const SemanticExpression& source,
        LoweringLiteralContext literal,
        ResultDemand demand,
        const LoweringValueConsumer& consume,
        LoweringStmtBuilder& destination,
        bool materializing = false
    ) noexcept -> void;
    auto can_extend_branch_scope(const SemanticExpression& source) const noexcept -> bool;
    enum class EvaluationForm { Expression, Statements, Branches };
    auto evaluation_form(const SemanticExpression& source) const noexcept -> EvaluationForm;
    auto external_exits(const SemanticExpression& source) const noexcept -> bool;
    auto value_region(
        const SemanticExpression& source,
        const std::function<void(LoweringResultDestination, LoweringStmtBuilder&)>& build
    ) noexcept -> Lowered<LoweringValue>;
    auto deliver_result(
        LoweringValue value,
        const LoweringResultDestination& result,
        LoweringStmtBuilder& destination
    ) noexcept -> void;
    auto initialize_deferred(
        const LoweringDeferredStorage& storage,
        TargetExpr initializer,
        LoweringStmtBuilder& destination
    ) noexcept -> void;
    auto full_expression(
        const SemanticExpression& source,
        LoweringLiteralContext use = LoweringLiteralContext::Exact
    ) noexcept -> Lowered<LoweringValue>;
    auto condition(const SemanticExpression& source) noexcept -> Lowered<LoweringPredicate>;
    auto cpp_call(const SemCppCall& call, std::vector<TargetExpr> operands) noexcept -> TargetExpr;
    auto cpp_operation(
        const SemanticExpression& source,
        const SemCpp& operation,
        std::vector<TargetExpr> operands
    ) noexcept -> TargetExpr;
    auto expression(
        const SemanticExpression& expression,
        LoweringLiteralContext use = LoweringLiteralContext::Exact,
        ResultDemand demand = ResultDemand::Value
    ) noexcept -> Lowered<LoweringValue>;
    enum class OperandUse { Snapshot, Read, Own, Place, ConstPlace };
    enum class OperandOrder { Unspecified, LeftToRight, Reordered, Postfix };
    auto materialize_operand(
        const SemanticExpression& source,
        TargetExpr value,
        OperandUse use,
        LoweringStmtBuilder& destination
    ) noexcept -> TargetExpr;
    auto operand(
        const SemanticExpression& expression,
        OperandUse use,
        LoweringLiteralContext literal = LoweringLiteralContext::Exact
    ) noexcept -> Lowered<TargetExpr>;

    struct Operand final {
        const SemanticExpression& expression;
        OperandUse use;
    };

    struct OperandGroup final {
        std::vector<Operand> values;
        OperandOrder order;
    };

    auto operation_operands(const SemanticExpression& source) const noexcept -> OperandGroup;
    auto requires_materialization(const OperandGroup& group, bool prefix) const noexcept -> bool;
    auto consume_operands(
        const OperandGroup& group,
        LoweringLiteralContext literal,
        bool materializing,
        const std::function<void(std::vector<TargetExpr>, LoweringStmtBuilder&)>& consume,
        LoweringStmtBuilder& destination
    ) noexcept -> void;
    auto statement(const SemanticStatement& statement) noexcept -> Lowered<LoweringUnit>;
    auto region(const SemanticRegion& region, LoweringResultDestination result) noexcept
        -> LoweringStmtBuilder;
    auto result_expression(
        const SemanticExpression& expression,
        LoweringResultDestination result,
        LoweringStmtBuilder& destination
    ) noexcept -> void;
    auto structured_expression(
        const SemanticExpression& expression,
        LoweringResultDestination result,
        LoweringStmtBuilder& destination
    ) noexcept -> void;
    auto guarded_region(
        const SemanticRegion& source,
        const std::optional<SemanticExpression>& guard,
        const LoweringResultDestination& result,
        RegionExit& done
    ) noexcept -> LoweringStmtBuilder;
    auto lower_if(
        const SemIf& value,
        LoweringResultDestination result,
        LoweringStmtBuilder& destination
    ) noexcept -> void;
    auto lower_match(
        const SemMatch& value,
        const LoweringResultDestination& result,
        LoweringStmtBuilder& destination
    ) noexcept -> void;
    auto lower_try(
        const SemTry& value,
        const LoweringResultDestination& result,
        LoweringStmtBuilder& destination
    ) noexcept -> void;
    auto lower_loop(const SemLoop& value, LoweringStmtBuilder& destination) noexcept -> void;
    auto lower_range(const SemRangeLoop& value, LoweringStmtBuilder& destination) noexcept -> void;
    auto lower_report(
        const SemTestReport& value,
        ProgramOriginID origin,
        LoweringStmtBuilder& destination
    ) noexcept -> void;
    auto emit_failure(TargetExpr value, LoweringStmtBuilder& destination) noexcept -> void;
    auto transfer_failure(
        const TargetIdentifier& storage,
        FailureSetID failures,
        LoweringStmtBuilder& destination
    ) noexcept -> void;
    auto emit_return(
        std::optional<TargetExpr> value,
        LoweringStmtBuilder& destination,
        LoweringResultDestination result = LoweringReturnResult {}
    ) noexcept -> void;
    auto binary(TargetExpr left, BinaryOperator operation, TargetExpr right, TypeID type) noexcept
        -> TargetExpr;
    auto field_identifier(StructID owner, std::uint32_t index) noexcept -> TargetIdentifier;
    auto binding_expression(LocalBindingID id) noexcept -> TargetExpr;
    auto declare_binding(
        LocalBindingID id,
        TargetExpr initializer,
        LoweringStmtBuilder& destination
    ) noexcept -> void;
    auto declare_deferred(
        const LoweringDeferredStorage& storage,
        bool maybe_unused,
        LoweringStmtBuilder& destination
    ) noexcept -> void;

    auto subject_expression(const PatternSubject& subject) noexcept -> TargetExpr;
    auto lower_pattern(PatternID pattern, PatternSubject subject) noexcept
        -> std::vector<PatternSelection>;
    auto cache_pattern_projections(
        std::vector<PatternSelection>& selections,
        std::vector<PatternProjection>& projections,
        LoweringStmtBuilder& destination
    ) noexcept -> void;
    auto pattern_condition(const PatternSelection& selection) noexcept -> std::optional<TargetExpr>;
    auto pattern_binding_expression(
        const PatternSelection& selection,
        LocalBindingID binding
    ) noexcept -> TargetExpr;
    ModuleLowering& context;
    const SemIRBody& body;
    TargetBodyInputs inputs;
    TargetNameAllocator names;
    std::vector<LocalBindingID> parameter_bindings;
    std::vector<LocalBindingID> capture_bindings;
    std::flat_map<LocalBindingID, TargetIdentifier> binding_names;
    std::flat_set<LocalBindingID> used_bindings;
    std::flat_set<LocalBindingID> taken_bindings;
    std::flat_map<LocalBindingID, LoweringDeferredStorage> delayed_bindings;
    std::optional<FailureDestination> failure_destination;
    std::optional<CaughtFailure> caught_failure;

    struct LoopContinuation final {
        std::optional<TargetIdentifier> step;
        LoweringExitTarget target;
        LoweringExitTarget break_target;
    };

    std::optional<LoopContinuation> loop_continuation;
    bool uses_test_context = false;
    std::size_t next_exit = 1;

    auto exit_target(LoweringExitKind kind) noexcept -> LoweringExitTarget {
        return {kind, next_exit++};
    }
};
