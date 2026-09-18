module carven:backend.realization.realizer;

import :backend.preparation.body;
import :backend.generation.names;
import :backend.lowering.body;
import :backend.lowering.constant;
import :backend.lowering.context;
import :backend.realization.composition;
import :backend.realization.pattern;
import :backend.target.expr;
import :backend.target.stmt;
import :semantic.semir.ids;
import :semantic.semir.structured;
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

    struct FailureDestination final {
        TargetIdentifier storage;
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
        ResultDemand demand = ResultDemand::Value
    ) noexcept -> Lowered<LoweringResult>;
    auto condition(const SemanticExpression& source) noexcept -> Lowered<LoweringPredicate>;
    auto operand(
        PreparedOperand source,
        ConstantLiteralContext literal = ConstantLiteralContext::Exact
    ) noexcept -> Lowered<TargetExpr>;
    auto discard(const SemanticExpression& source) noexcept -> Lowered<LoweringCompleted>;
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
        -> void;
    auto pattern_bound(
        std::span<const SemPatternBounds> pattern_bounds,
        PatternID pattern,
        bool upper,
        LoweringStmtBuilder& destination
    ) noexcept -> std::optional<TargetExpr>;
    auto assign(const SemAssign& source, LoweringStmtBuilder& destination) noexcept -> void;
    auto statement(const SemanticStatement& source) noexcept -> Lowered<LoweringCompleted>;
    auto region(const SemanticRegion& source, const LoweringResultDestination& result) noexcept
        -> LoweringStmtBuilder;
    auto result_expression(
        const SemanticExpression& source,
        const LoweringResultDestination& result,
        LoweringStmtBuilder& destination
    ) noexcept -> void;
    auto structured_delivery(
        const SemanticExpression& source,
        const LoweringResultDestination& result,
        LoweringStmtBuilder& destination
    ) noexcept -> void;
    auto structured_expression(
        const SemanticExpression& source,
        const LoweringResultDestination& result,
        LoweringStmtBuilder& destination
    ) noexcept -> void;
    auto guarded_region(
        const SemanticRegion& source,
        const std::optional<SemanticExpression>& guard,
        const LoweringResultDestination& result,
        RegionExit& done
    ) noexcept -> LoweringStmtBuilder;
    auto lower_arm(
        const PatternState& pattern,
        std::span<const LocalBindingID> bindings,
        const SemanticRegion& source,
        const std::optional<SemanticExpression>& guard,
        const LoweringResultDestination& result,
        RegionExit& done
    ) noexcept -> LoweringStmtBuilder;
    auto lower_if(
        const SemIf& value,
        const LoweringResultDestination& result,
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
        const std::optional<FailureDestination>& exit
    ) noexcept -> LoweringStmtBuilder;
    auto transfer_failure(
        const TargetIdentifier& storage,
        FailureSetID failures,
        const std::optional<FailureDestination>& exit,
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

    struct TestObservation final {
        const SemanticExpression* expression;
        TargetIdentifier writer;
        std::array<ProgramSpellingID, 2> sources;
    };

    std::optional<TestObservation> test_observation;
    ExpressionBuilder* active_frame = nullptr;
    ModuleLowering& context;
    const BodyPreparation& preparation;
    const SemIRBody& metadata;
    BodyLoweringInputs inputs;
    TargetNameAllocator names;
    std::flat_map<LocalBindingID, TargetIdentifier> binding_names;
    std::flat_set<std::string> mutable_owners;
    std::flat_map<LocalBindingID, LoweringDeferredStorage> delayed_bindings;
    std::optional<FailureDestination> current_failure;

    struct CaughtFailure final {
        TargetIdentifier storage;
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
