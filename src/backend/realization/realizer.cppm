module carven:backend.realization.realizer;

import :backend.generation.names;
import :backend.lowering.body;
import :backend.lowering.constant;
import :backend.lowering.context;
import :backend.preparation.body;
import :backend.realization.composition;
import :backend.realization.pattern;
import :backend.target.expr;
import :backend.target.stmt;
import :semantic.semir.ids;
import :semantic.semir.structured;
import :support.task;
import std;

class BodyRealizer final {
public:
    BodyRealizer(
        ModuleLowering& context,
        const BodyPreparation& preparation,
        BodyLoweringInputs inputs
    ) noexcept;
    auto finish() noexcept -> LoweredBody;

private:
    class ExpressionBuilder;
    static constexpr auto callable_scope = TargetScopeID {.ordinal = 0};

    struct FailureSlot final {
        TargetLocalID storage;
        FailureSetID layout;
    };

    struct FailureDestination final {
        FailureSlot slot;
        TargetIdentifier label;
        LoweringExitTarget target;
    };

    struct RegionExit final {
        TargetIdentifier label;
        LoweringExitTarget target;
    };

    enum class ResultDemand { Value, DirectReturn, Discard, PropagateOutcome };
    auto expression(
        const SemanticExpression& source,
        ConstantLiteralContext literal = ConstantLiteralContext::Exact,
        ResultDemand demand = ResultDemand::Value,
        std::optional<LifetimeRegionID> delivered_region = std::nullopt
    ) noexcept -> ContinuationTask<Lowered<LoweringResult>>;
    auto condition(const SemanticExpression& source) noexcept
        -> ContinuationTask<Lowered<LoweringPredicate>>;
    auto operand(
        PreparedOperand source,
        ConstantLiteralContext literal = ConstantLiteralContext::Exact
    ) noexcept -> ContinuationTask<Lowered<TargetExpr>>;
    auto discard(const SemanticExpression& source) noexcept
        -> ContinuationTask<Lowered<LoweringCompleted>>;
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
    auto initialize_binding(const SemInitialize& source, LoweringStmtBuilder& destination) noexcept
        -> ContinuationTask<std::monostate>;
    auto pattern_bound(
        std::span<const SemPatternBounds> pattern_bounds,
        PatternID pattern,
        bool upper,
        LoweringStmtBuilder& destination
    ) noexcept -> ContinuationTask<std::optional<TargetExpr>>;
    auto assign(const SemAssign& source, LoweringStmtBuilder& destination) noexcept
        -> ContinuationTask<std::monostate>;
    auto statement(const SemanticStatement& source) noexcept
        -> ContinuationTask<Lowered<LoweringCompleted>>;
    auto region(const SemanticRegion& source, const LoweringResultDestination& result) noexcept
        -> ContinuationTask<LoweringStmtBuilder>;
    auto result_expression(
        const SemanticExpression& source,
        const LoweringResultDestination& result,
        LoweringStmtBuilder& destination,
        std::optional<LifetimeRegionID> delivered_region = std::nullopt
    ) noexcept -> ContinuationTask<std::monostate>;
    auto structured_delivery(
        const SemanticExpression& source,
        const LoweringResultDestination& result,
        LoweringStmtBuilder& destination
    ) noexcept -> ContinuationTask<std::monostate>;
    auto structured_expression(
        const SemanticExpression& source,
        const LoweringResultDestination& result,
        LoweringStmtBuilder& destination
    ) noexcept -> ContinuationTask<std::monostate>;
    auto guarded_region(
        const SemanticRegion& source,
        const std::optional<SemanticExpression>& guard,
        const LoweringResultDestination& result,
        RegionExit& done
    ) noexcept -> ContinuationTask<LoweringStmtBuilder>;
    auto lower_arm(
        const PatternBindings& pattern,
        LoweringPredicate predicate,
        std::span<const LocalBindingID> bindings,
        const SemanticRegion& source,
        const std::optional<SemanticExpression>& guard,
        const LoweringResultDestination& result,
        RegionExit& done
    ) noexcept -> ContinuationTask<LoweringStmtBuilder>;
    auto lower_if(
        const SemIf& value,
        const LoweringResultDestination& result,
        LoweringStmtBuilder& destination
    ) noexcept -> ContinuationTask<std::monostate>;
    auto lower_match(
        const SemMatch& value,
        const LoweringResultDestination& result,
        LoweringStmtBuilder& destination
    ) noexcept -> ContinuationTask<std::monostate>;
    auto lower_try(
        const SemTry& value,
        const LoweringResultDestination& result,
        LoweringStmtBuilder& destination
    ) noexcept -> ContinuationTask<std::monostate>;
    auto lower_loop(const SemLoop& value, LoweringStmtBuilder& destination) noexcept
        -> ContinuationTask<std::monostate>;
    auto lower_range(const SemRangeLoop& value, LoweringStmtBuilder& destination) noexcept
        -> ContinuationTask<std::monostate>;
    auto lower_report(
        const SemReport& value,
        ProgramOriginID origin,
        LoweringStmtBuilder& destination
    ) noexcept -> ContinuationTask<std::monostate>;
    auto emit_test_exit(LoweringStmtBuilder& destination) noexcept -> void;
    auto emit_return(
        std::optional<TargetExpr> value,
        LoweringStmtBuilder& destination,
        const LoweringResultDestination& result = LoweringReturnResult {}
    ) noexcept -> void;
    auto emit_failure(
        TargetExpr value,
        const std::optional<FailureDestination>& exit,
        LoweringStmtBuilder& destination
    ) noexcept -> void;

    struct OutcomeFailureSource final {
        TargetLocalID storage;
        bool deferred;
    };

    using FailureSource = std::variant<OutcomeFailureSource, FailureSlot>;
    auto failure_projection(FailureSlot slot, TypeID type) noexcept -> TargetExpr;
    auto dispatch_failure(
        const FailureSource& source,
        FailureSetID failures,
        const std::optional<FailureDestination>& exit
    ) noexcept -> LoweringStmtBuilder;
    auto transfer_failure(
        FailureSlot slot,
        FailureSetID failures,
        const std::optional<FailureDestination>& exit,
        LoweringStmtBuilder& destination
    ) noexcept -> void;
    auto fresh_local(TargetTemporaryNameKind kind) noexcept -> TargetLocalID;
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

    struct ConditionObservation final {
        const SemanticExpression* expression;
        TargetLocalID writer;
        std::array<ProgramSpellingID, 2> sources;
    };

    std::optional<ConditionObservation> condition_observation;
    ExpressionBuilder* active_frame = nullptr;
    ModuleLowering& context;
    const BodyPreparation& preparation;
    const SemIRBody& metadata;
    BodyLoweringInputs inputs;
    TargetNameAllocator names;
    std::flat_map<LocalBindingID, TargetLocalID> binding_locals;
    std::flat_map<LocalBindingID, TargetIdentifier> capture_names;
    std::flat_set<TargetLocalID> mutable_owners;
    std::flat_set<TargetLocalID> removable_locals;
    std::flat_map<LocalBindingID, LoweringDeferredStorage> delayed_bindings;
    std::optional<FailureDestination> current_failure;

    struct CaughtFailure final {
        FailureSlot slot;
        FailureSetID failures;
    };

    std::optional<CaughtFailure> caught;

    struct FallibleCall final {
        FailureSetID failures;
        std::optional<FailureDestination> destination;
    };

    auto fallible(const SemanticExpression& source) const noexcept -> std::optional<FallibleCall>;

    struct LoopContinuation final {
        std::optional<TargetIdentifier> step;
        LoweringExitTarget target;
        LoweringExitTarget break_target;
    };

    std::optional<LoopContinuation> current_loop;
    std::size_t next_exit = 1;

    auto exit_target(LoweringExitKind kind) noexcept -> LoweringExitTarget;
};
