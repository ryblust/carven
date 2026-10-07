module carven:backend.realization.realizer;

import :backend.generation.names;
import :backend.lowering.body;
import :backend.lowering.constant;
import :backend.lowering.context;
import :backend.preparation.async;
import :backend.preparation.body;
import :backend.realization.composition;
import :backend.realization.decl;
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
        BodyLoweringInputs inputs,
        BodyRealizer* enclosing = nullptr,
        std::optional<LoweringResultDestination> local_result = std::nullopt
    ) noexcept;
    auto finish() noexcept -> LoweredBody;

private:
    class ExpressionBuilder;
    class FailureSiteQuery;
    static constexpr auto callable_scope = TargetScopeID {.ordinal = 0};

    auto is_async() const noexcept -> bool;
    auto completion_type() noexcept -> TargetTypeID;

    auto select_await_producer(const SemanticExpression& source) noexcept
        -> std::optional<PreparedAwaitProducer>;
    auto emit_tail_await(const SemAwait& source, LoweringStmtBuilder& destination) noexcept
        -> ContinuationTask<std::monostate>;
    auto wrap_tail_loop(LoweringStmtBuilder statements) noexcept -> LoweringStmtBuilder;
    auto begin_async_region(const SemanticRegion& source, LoweringStmtBuilder& destination) noexcept
        -> void;
    auto close_async_scopes(
        std::size_t retained,
        bool cancel,
        LoweringStmtBuilder& destination
    ) noexcept -> void;
    auto close_async_locals(
        std::span<const TargetLocalID> scopes,
        bool cancel,
        LoweringStmtBuilder& destination
    ) noexcept -> void;
    auto async_closing_locals(std::size_t retained) const noexcept -> std::vector<TargetLocalID>;
    auto leave_async_scopes(std::size_t retained) noexcept -> void;
    auto emit_completion(
        TargetExpr completion,
        LoweringExitKind kind,
        LoweringStmtBuilder& destination
    ) noexcept -> void;
    auto emit_cancelled(LoweringStmtBuilder& destination) noexcept -> void;
    auto emit_async_exit(
        TargetStmt continuation,
        LoweringExitTarget target,
        std::vector<TargetLocalID> closing_scopes,
        bool cancel,
        LoweringStmtBuilder& destination
    ) noexcept -> void;
    auto realize_async_exits(std::vector<TargetStmt>& statements) noexcept -> void;

    struct AsyncExit final {
        TargetIdentifier label;
        std::vector<TargetLocalID> closing_scopes;
        bool cancel;
        TargetStmt continuation;
    };

    std::map<std::string, AsyncExit> async_exits;
    std::optional<LoweringDeferredStorage> pending_completion;

    struct FailureSlot final {
        TargetLocalID storage;
        FailureSetID layout;
    };

    struct FailureDestination final {
        std::size_t identity;
        LoweringExitTarget target;
        std::size_t retained_async_scopes;
    };

    struct FailureRelay final {
        FailureSlot slot;
        TargetIdentifier label;
        LoweringExitTarget target;
        std::vector<TargetLocalID> closing_scopes;
    };

    struct RegionExit final {
        TargetIdentifier label;
        LoweringExitTarget target;
    };

    enum class ResultDemand { Value, DirectReturn, Discard, PropagateOutcome, AdoptSuccess };
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
    auto region(
        const SemanticRegion& source,
        const LoweringResultDestination& result,
        bool retain_async_scope = false
    ) noexcept -> ContinuationTask<LoweringStmtBuilder>;
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
        const PatternSelection& pattern,
        std::span<const LocalBindingID> bindings,
        const SemanticRegion& source,
        const std::optional<SemanticExpression>& guard,
        const LoweringResultDestination& result,
        RegionExit* done
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
    auto lower_expanded_loop(
        const SemExpandedLoop& value,
        LoweringStmtBuilder& destination
    ) noexcept -> ContinuationTask<std::monostate>;
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
        const SemanticExpression& source,
        const std::optional<FailureDestination>& exit,
        LoweringStmtBuilder& destination
    ) noexcept -> void;
    auto deliver_failure(
        TargetExpr value,
        const std::optional<FailureRelay>& relay,
        LoweringStmtBuilder& destination
    ) noexcept -> void;

    struct OutcomeFailureSource final {
        TargetLocalID storage;
        bool deferred;
        FailureSetID layout;
    };

    struct ValueFailureSource final {
        TargetLocalID storage;
        TypeID type;
    };

    using FailureSource = std::variant<OutcomeFailureSource, FailureSlot, ValueFailureSource>;

    struct FailureEdge final {
        TargetIdentifier label;
        FailureSource source;
        FailureSetID failures;
        // A throw value remains at its evaluation site until receiver layout is known.
        std::optional<TargetExpr> initializer;
        std::vector<TargetLocalID> closing_scopes;
    };

    struct FailureReceiver final {
        FailureSetID layout;
        std::vector<FailureEdge> edges;
    };

    auto failure_contains(const FailureSource& source, TypeID type) const noexcept -> bool;
    auto failure_projection(const FailureSource& source, TypeID type) noexcept -> TargetExpr;
    auto register_failure(
        FailureSource source,
        FailureSetID failures,
        const FailureDestination& receiver,
        LoweringStmtBuilder& destination,
        std::optional<TargetExpr> initializer = std::nullopt
    ) noexcept -> void;
    auto dispatch_failure(
        const FailureSource& source,
        FailureSetID failures,
        const std::optional<FailureRelay>& relay
    ) noexcept -> LoweringStmtBuilder;
    auto transfer_failure(
        const FailureSource& source,
        FailureSetID failures,
        const std::optional<FailureDestination>& exit,
        LoweringStmtBuilder& destination
    ) noexcept -> void;
    auto failure_handler(
        const SemTry& value,
        const FailureSource& source,
        FailureSetID failures,
        const LoweringResultDestination& result,
        RegionExit& done,
        const std::optional<FailureDestination>& outer
    ) noexcept -> ContinuationTask<LoweringStmtBuilder>;
    auto fresh_local(TargetTemporaryNameKind kind) noexcept -> TargetLocalID;
    auto binding_expression(LocalBindingID id) noexcept -> TargetExpr;
    auto needs_cleanup(TypeID type) const noexcept -> bool;
    auto declare_binding(
        LocalBindingID id,
        TargetExpr initializer,
        LoweringStmtBuilder& destination,
        bool snapshot = false
    ) noexcept -> void;
    auto declare_deferred(
        const LoweringDeferredStorage& storage,
        bool maybe_unused,
        LoweringStmtBuilder& destination,
        bool needs_cleanup
    ) noexcept -> void;

    struct ConditionObservation final {
        const SemanticExpression* expression;
        TargetLocalID writer;
        std::array<ProgramSpellingID, 2> sources;
    };

    struct AsyncScopeStorage final {
        LifetimeRegionID lifetime;
        TargetLocalID scope;
    };

    std::vector<AsyncScopeStorage> async_scopes;
    std::flat_set<LocalBindingID> success_bindings;
    std::optional<ConditionObservation> condition_observation;
    ExpressionBuilder* active_frame = nullptr;
    ModuleLowering& context;
    const BodyPreparation& preparation;
    const SemIRBody& metadata;
    BodyLoweringInputs inputs;
    TargetNameAllocator owned_names;
    TargetNameAllocator& names;

    struct FusionState final {
        std::size_t remaining;
        std::vector<CallableID> active;
    };

    FusionState owned_fusion;
    FusionState& fusion;

    struct LocalResultExit final {
        LoweringResultDestination result;
        RegionExit exit;
    };

    struct TailAwaitLoop final {
        PreparedTailAwaitLoop selection;
        std::vector<TargetLocalID> slots;
        LoweringExitTarget next;
        TargetIdentifier next_label;
    };

    std::optional<TailAwaitLoop> tail_loop;
    std::optional<LocalResultExit> local_result_exit;
    std::flat_map<LocalBindingID, TargetLocalID> binding_locals;
    std::flat_map<LocalBindingID, TargetIdentifier> capture_names;
    std::flat_set<TargetLocalID> mutable_owners;
    std::flat_map<TargetLocalID, UnusedInitializer> unused_initializers;
    std::flat_map<LocalBindingID, LoweringDeferredStorage> delayed_bindings;
    std::optional<FailureDestination> current_failure;
    std::vector<FailureReceiver> failure_receivers;

    struct CaughtFailure final {
        FailureSource source;
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
        std::optional<TargetIdentifier> break_label;
        TargetJumpRole jump_role;
        bool expanded;
        LoweringExitTarget target;
        LoweringExitTarget break_target;
        std::size_t break_async_scopes;
        std::size_t continue_async_scopes;
    };

    std::optional<LoopContinuation> current_loop;
    std::size_t next_exit = 1;

    auto exit_target(LoweringExitKind kind) noexcept -> LoweringExitTarget;
};
