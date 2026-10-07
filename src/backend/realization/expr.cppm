module carven:backend.realization.expr;

import :backend.lowering.constant;
import :backend.lowering.context;
import :backend.preparation.body;
import :backend.realization.operation;
import :backend.realization.realizer;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.type;
import :semantic.semir.ids;
import :semantic.semir.structured;
import :support.invariant;
import :support.task;
import std;

// A source cleanup frame composes completed child fragments. BuildScope isolates
// output chains during child construction; source-order adoption determines
// declaration and execution order. Storage escaping a nested C++ scope exports
// reservations to its controlling operation in the source cleanup scope.
class BodyRealizer::ExpressionBuilder final {
public:
    ExpressionBuilder(
        BodyRealizer& owner,
        const SemanticExpression& source,
        std::optional<LifetimeRegionID> delivered_region = std::nullopt
    ) noexcept;
    ~ExpressionBuilder() noexcept;
    auto owns(const SemanticExpression& source) const noexcept -> bool;
    auto evaluate(
        const SemanticExpression& source,
        ConstantLiteralContext literal,
        ResultDemand demand,
        PreparedUse use
    ) noexcept -> ContinuationTask<Lowered<LoweringResult>>;
    auto deliver_structured(
        const SemanticExpression& source,
        const LoweringResultDestination& result,
        LoweringStmtBuilder& destination,
        bool shared
    ) noexcept -> ContinuationTask<std::monostate>;
    auto finish_expression(
        const SemanticExpression& source,
        ConstantLiteralContext literal,
        ResultDemand demand,
        PreparedUse final_use = PreparedUse::Consume,
        bool shared = false
    ) noexcept -> ContinuationTask<Lowered<LoweringResult>>;
    auto initialize_expression(
        const SemInitialize& initialization,
        LoweringStmtBuilder& destination
    ) noexcept -> ContinuationTask<std::monostate>;
    auto assign(const SemAssign& assignment, LoweringStmtBuilder& destination) noexcept
        -> ContinuationTask<std::monostate>;

private:
    enum class SavedKind { Value, Place, StoredValue, StoredPlace, Success };

    struct BuildRequest final {
        ResultDemand demand;
        PreparedUse use;
        ConstantLiteralContext literal;
        bool retain_backing;
        std::size_t expression_depth;
        // Only an immediate await consumes an embedded producer's ordinary result.
        std::optional<PreparedAwaitProducer> await_fusion = std::nullopt;
    };

    struct Saved final {
        TargetLocalID local;
        SavedKind kind;
    };

    // Bindings and completed constant facts can be referenced repeatedly without
    // reconstructing an executed expression. Every other inline tree is moved once.
    struct AwaitedCarrier final {
        TargetLocalID local;
        bool deferred;
        TypeID operation;
    };

    struct Fragment final {
        PreparedOperation preparation;
        std::variant<LocalBindingID, ConstantID, Saved, TargetExpr, LoweringCompleted> completion;
        LoweringStmtBuilder reservations;
        LoweringStmtBuilder statements;
        bool local_storage;
        bool executes;
        bool observes;
        std::optional<AwaitedCarrier> awaited_carrier;
    };

    class BuildScope final {
    public:
        explicit BuildScope(ExpressionBuilder& frame) noexcept;
        ~BuildScope() noexcept;

    private:
        ExpressionBuilder& frame;
        LoweringStmtBuilder reservations;
        LoweringStmtBuilder statements;
    };

    class StorageScope final {
    public:
        explicit StorageScope(ExpressionBuilder& frame, bool nested = true) noexcept;
        ~StorageScope() noexcept;

    private:
        ExpressionBuilder& frame;
        bool local_storage;
    };

    BodyRealizer& owner;
    LifetimeRegionID cleanup;
    ExpressionBuilder* previous_frame;
    bool local_storage;
    LoweringStmtBuilder reservations;
    LoweringStmtBuilder statements;

    auto finish_fragment(Fragment value) noexcept -> Fragment;
    auto adopt(Fragment& value) noexcept -> void;
    auto take_statements(bool shared = false) noexcept -> LoweringStmtBuilder;
    auto owns_cleanup_scope(std::optional<LifetimeRegionID> delivered_region) const noexcept
        -> bool;

    template<typename T>
    static auto complete(Fragment& value, T result) noexcept -> void {
        value.completion = std::move(result);
    }

    auto saved(const Fragment& value) const noexcept -> const Saved*;
    auto source(const Fragment& value) const noexcept -> const PreparedOperation&;
    auto scalar(TypeID id) const noexcept -> bool;
    auto names_storage(const PreparedOperation& value) const noexcept -> bool;
    auto stable_place(const SemanticExpression& value) const noexcept -> bool;
    auto borrowed_owner(const PreparedOperation& value, PreparedUse use) const noexcept -> bool;
    auto const_parameter(const Fragment& fragment) const noexcept -> bool;
    auto pending(const Fragment& value) const noexcept -> bool;
    auto discard_pending(Fragment& value) noexcept -> void;
    auto has_storage_read(const Fragment& value) const noexcept -> bool;
    auto has_effect(const Fragment& value) const noexcept -> bool;
    static auto value_binding(PreparedUse use) noexcept -> TargetVariableBinding;
    auto build(const SemanticExpression& expression, BuildRequest request) noexcept
        -> ContinuationTask<Fragment>;
    auto complete_writer(
        Fragment& value,
        const SemFormat& format,
        const PreparedWriterFormat& preparation,
        std::span<Fragment> operands,
        std::span<const PreparedOperand> inputs,
        std::optional<TargetLocalID> output
    ) noexcept -> void;
    auto sequenced_suffix_begin(const PreparedOperation& value) const noexcept -> std::size_t;
    auto first_unsequenced(const PreparedOperation& value) const noexcept -> std::size_t;
    auto retain_input(Fragment& value, PreparedUse use) noexcept -> void;
    auto raw(
        Fragment& value,
        ConstantLiteralContext literal = ConstantLiteralContext::Exact
    ) noexcept -> TargetExpr;
    auto emit(
        Fragment& value,
        PreparedUse use,
        ConstantLiteralContext literal = ConstantLiteralContext::Exact
    ) noexcept -> TargetExpr;
    auto anchor(Fragment& value, PreparedUse use, bool force = false) noexcept -> void;
    static auto native_await_intrinsic(const SemAwait& source) noexcept -> const SemAsyncIntrinsic*;
    auto complete_await(
        Fragment& value,
        TargetExpr invocation,
        const SemAwait& source,
        bool project_success,
        PreparedUse use
    ) noexcept -> void;
    auto complete_awaited(
        Fragment& value,
        TargetExpr completion,
        const SemAwait& source,
        bool project_success,
        PreparedUse use
    ) noexcept -> void;
    auto snapshot_parameters(
        CallableID callable,
        BodyID body,
        std::vector<TargetExpr> operands
    ) noexcept -> BodyLoweringInputs;
    auto fuse_call(
        const SemColdCall& call,
        BodyID body,
        std::vector<TargetExpr> operands,
        ResultDemand demand
    ) noexcept -> std::optional<TargetLocalID>;
    auto fuse_factory(
        const SemCall& call,
        const PreparedAwaitProducer& producer,
        std::vector<TargetExpr> operands,
        ResultDemand demand
    ) noexcept -> std::optional<TargetLocalID>;
    auto complete_call(
        Fragment& value,
        const FallibleCall& transport,
        bool project_success,
        PreparedUse use,
        bool propagate_outcome
    ) noexcept -> void;
};
