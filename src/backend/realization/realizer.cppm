module carven:backend.realization.realizer;

import :backend.construction;
import :backend.generation.names;
import :backend.lowering.context;
import :backend.realization.body;
import :backend.realization.composition;
import :backend.realization.constant;
import :backend.realization.pattern;
import :backend.target.expr;
import :backend.target.stmt;
import :semantic.semir;
import std;

class BodyRealizer final {
public:
    BodyRealizer(
        ModuleLowering& context,
        const BodyConstruction& construction,
        BodyRealizationInputs inputs
    ) noexcept;
    auto finish() noexcept -> LoweredBody;

private:
    class ExpressionBuilder;
    static constexpr auto callable_scope = TargetScopeID {.ordinal = 0};

    struct FailureDestination final {
        TargetIdentifier storage;
        TargetIdentifier label;
        LoweringExitTarget target;
    };

    struct RegionExit final {
        TargetIdentifier label;
        LoweringExitTarget target;
    };


    enum class ResultDemand { Value, Observe, Discard };
    auto expression(
        ConstructionExpressionID source,
        RealizationLiteralContext literal = RealizationLiteralContext::Exact,
        ResultDemand demand = ResultDemand::Value
    ) noexcept -> Lowered<LoweringResult>;
    auto condition(ConstructionExpressionID source) noexcept -> Lowered<LoweringPredicate>;
    auto operand(
        ConstructionOperand source,
        RealizationLiteralContext literal = RealizationLiteralContext::Exact
    ) noexcept -> Lowered<TargetExpr>;
    auto discard(ConstructionExpressionID source) noexcept -> Lowered<LoweringCompleted>;
    auto read_value(Lowered<LoweringResult> value, LoweringStmtBuilder& destination) noexcept
        -> std::optional<TargetExpr>;
    auto deliver_result(
        LoweringResult value,
        const LoweringResultDestination& result,
        LoweringStmtBuilder& destination
    ) noexcept -> void;
    auto initialize_deferred(
        const LoweringDeferredStorage& storage,
        TargetExpr initializer,
        LoweringStmtBuilder& destination,
        std::optional<TargetTypeID> factory_result = std::nullopt
    ) noexcept -> void;
    auto initialize_binding(
        const ConstructionInitialize& source,
        LoweringStmtBuilder& destination
    ) noexcept -> void;
    auto assign(const ConstructionAssign& source, LoweringStmtBuilder& destination) noexcept
        -> void;
    auto statement(const ConstructionStatement& source, ConstructionRegionID owner) noexcept
        -> Lowered<LoweringCompleted>;
    auto region(ConstructionRegionID source, const LoweringResultDestination& result) noexcept
        -> LoweringStmtBuilder;
    auto result_expression(
        ConstructionExpressionID source,
        const LoweringResultDestination& result,
        LoweringStmtBuilder& destination
    ) noexcept -> void;
    auto structured_delivery(
        ConstructionExpressionID source,
        const LoweringResultDestination& result,
        LoweringStmtBuilder& destination
    ) noexcept -> void;
    auto structured_expression(
        ConstructionExpressionID source,
        const LoweringResultDestination& result,
        LoweringStmtBuilder& destination
    ) noexcept -> void;
    auto guarded_region(
        ConstructionRegionID source,
        const std::optional<ConstructionExpressionID>& guard,
        const LoweringResultDestination& result,
        RegionExit& done
    ) noexcept -> LoweringStmtBuilder;
    auto lower_arm(
        const PatternState& pattern,
        std::span<const LocalBindingID> bindings,
        ConstructionRegionID source,
        const std::optional<ConstructionExpressionID>& guard,
        const LoweringResultDestination& result,
        RegionExit& done
    ) noexcept -> LoweringStmtBuilder;
    auto lower_if(
        const ConstructionConditional& value,
        const LoweringResultDestination& result,
        LoweringStmtBuilder& destination
    ) noexcept -> void;
    auto lower_match(
        const ConstructionMatch& value,
        const LoweringResultDestination& result,
        LoweringStmtBuilder& destination
    ) noexcept -> void;
    auto lower_try(
        ConstructionExpressionID identity,
        const ConstructionTry& value,
        const LoweringResultDestination& result,
        LoweringStmtBuilder& destination
    ) noexcept -> void;
    auto lower_loop(
        ConstructionRegionID identity,
        const ConstructionLoop& value,
        LoweringStmtBuilder& destination
    ) noexcept -> void;
    auto lower_range(
        ConstructionRegionID identity,
        const ConstructionRangeLoop& value,
        LoweringStmtBuilder& destination
    ) noexcept -> void;
    auto lower_report(
        const ConstructionTestReport& value,
        ProgramOriginID origin,
        LoweringStmtBuilder& destination
    ) noexcept -> void;
    auto emit_test_exit(LoweringStmtBuilder& destination) noexcept -> void;
    auto emit_return(
        std::optional<TargetExpr> value,
        LoweringStmtBuilder& destination,
        const LoweringResultDestination& result = LoweringReturnResult {}
    ) noexcept -> void;
    auto emit_failure(
        TargetExpr value,
        const ConstructionFailureExit& exit,
        LoweringStmtBuilder& destination
    ) noexcept -> void;

    struct OutcomeFailureSource final {
        TargetIdentifier storage;
        bool deferred;
    };

    struct VariantFailureSource final {
        TargetIdentifier storage;
    };

    using FailureSource = std::variant<OutcomeFailureSource, VariantFailureSource>;
    auto dispatch_failure(
        const FailureSource& source,
        FailureSetID failures,
        const ConstructionFailureExit& exit
    ) noexcept -> LoweringStmtBuilder;
    auto transfer_failure(
        const TargetIdentifier& storage,
        FailureSetID failures,
        const ConstructionFailureExit& exit,
        LoweringStmtBuilder& destination
    ) noexcept -> void;
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
    auto pattern_bindings(std::span<const LocalBindingID> bindings) const noexcept
        -> std::vector<PatternBindingType>;
    auto pattern_branch(
        TargetExpr condition,
        LoweringStmtBuilder selected,
        LoweringStmtBuilder& destination
    ) noexcept -> void;

    ExpressionBuilder* active_frame = nullptr;
    ModuleLowering& context;
    const BodyConstruction& construction;
    const SemIRBody& metadata;
    BodyRealizationInputs inputs;
    TargetNameAllocator names;
    std::vector<LocalBindingID> parameter_bindings;
    std::vector<LocalBindingID> capture_bindings;
    std::flat_map<LocalBindingID, TargetIdentifier> binding_names;
    std::flat_set<LocalBindingID> taken_bindings;
    std::flat_map<LocalBindingID, LoweringDeferredStorage> delayed_bindings;
    std::map<ConstructionExpressionID, FailureDestination> handlers;

    struct LoopContinuation final {
        std::optional<TargetIdentifier> step;
        LoweringExitTarget target;
        LoweringExitTarget break_target;
    };

    std::map<ConstructionRegionID, LoopContinuation> loops;
    std::size_t next_exit = 1;

    auto exit_target(LoweringExitKind kind) noexcept -> LoweringExitTarget;
};
