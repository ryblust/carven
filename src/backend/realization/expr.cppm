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
// declaration and execution order. Structured regions sharing a source lifetime
// retain declarations in the frame and initialize them in the selected branch.
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

    struct Saved final {
        TargetLocalID local;
        SavedKind kind;
    };

    // Bindings and completed constant facts can be referenced repeatedly without
    // reconstructing an executed expression. Every other inline tree is moved once.
    struct Fragment final {
        PreparedOperation preparation;
        std::variant<LocalBindingID, ConstantID, Saved, TargetExpr, LoweringCompleted> completion;
        LoweringStmtBuilder declarations;
        LoweringStmtBuilder statements;
        bool executes;
        bool observes;
    };

    class BuildScope final {
    public:
        explicit BuildScope(ExpressionBuilder& frame) noexcept;
        ~BuildScope() noexcept;

    private:
        ExpressionBuilder& frame;
        LoweringStmtBuilder declarations;
        LoweringStmtBuilder statements;
    };

    BodyRealizer& owner;
    LifetimeRegionID cleanup;
    ExpressionBuilder* previous_frame;
    bool independent_scope;
    bool automatic_storage;
    LoweringStmtBuilder declarations;
    LoweringStmtBuilder statements;

    auto finish_fragment(Fragment value) noexcept -> Fragment;
    auto adopt(Fragment& value) noexcept -> void;
    auto take_statements(bool shared = false) noexcept -> LoweringStmtBuilder;
    auto has_independent_scope(std::optional<LifetimeRegionID> delivered_region) const noexcept
        -> bool;

    template<typename T>
    static auto complete(Fragment& value, T result) noexcept -> void {
        value.completion = std::move(result);
    }

    auto saved(const Fragment& value) const noexcept -> const Saved*;
    auto source(const Fragment& value) const noexcept -> const PreparedOperation&;
    auto scalar(TypeID id) const noexcept -> bool;
    auto names_storage(const PreparedOperation& value) const noexcept -> bool;
    auto stable_place_binding(const SemanticExpression& value) const noexcept -> bool;
    auto borrowed_owner(const PreparedOperation& value, PreparedUse use) const noexcept -> bool;
    auto pending(const Fragment& value) const noexcept -> bool;
    auto discard_pending(Fragment& value) noexcept -> void;
    auto has_storage_read(const Fragment& value) const noexcept -> bool;
    auto has_effect(const Fragment& value) const noexcept -> bool;
    static auto value_binding(PreparedUse use) noexcept -> TargetVariableBinding;
    auto build(
        const SemanticExpression& expression,
        bool result_needed = true,
        PreparedUse result_use = PreparedUse::Consume,
        bool propagate_outcome = false,
        ConstantLiteralContext literal = ConstantLiteralContext::Exact,
        bool direct_return = false,
        bool retain_backing = true,
        std::size_t expression_depth = 0
    ) noexcept -> ContinuationTask<Fragment>;
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
    auto anchor(
        Fragment& value,
        PreparedUse use,
        bool force = false,
        bool direct_scalar = false
    ) noexcept -> void;
    auto complete_call(
        Fragment& value,
        const FallibleCall& transport,
        bool project_success,
        PreparedUse use,
        bool propagate_outcome
    ) noexcept -> void;
};
