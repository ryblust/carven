module carven:backend.realization.expr;

import :backend.preparation.body;
import :backend.lowering.constant;
import :backend.lowering.context;
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

// Execution buffers may nest while declarations remain with the source cleanup
// frame identified by LifetimeRegionID.
class BodyRealizer::ExpressionBuilder final {
public:
    ExpressionBuilder(BodyRealizer& owner, const SemanticExpression& source) noexcept;

    ~ExpressionBuilder() noexcept;
    auto owns(const SemanticExpression& source) const noexcept -> bool;
    auto evaluate(
        const SemanticExpression& source,
        ConstantLiteralContext literal,
        ResultDemand demand,
        PreparedUse use
    ) noexcept -> Lowered<LoweringResult>;
    auto deliver_structured(
        const SemanticExpression& source,
        const LoweringResultDestination& result,
        LoweringStmtBuilder& destination,
        bool shared
    ) noexcept -> void;
    auto finish(
        const SemanticExpression& source,
        ConstantLiteralContext literal,
        ResultDemand demand,
        PreparedUse final_use = PreparedUse::Consume,
        bool shared = false
    ) noexcept -> Lowered<LoweringResult>;
    auto initialize(const SemInitialize& initialization, LoweringStmtBuilder& destination) noexcept
        -> void;
    auto assign(const SemAssign& assignment, LoweringStmtBuilder& destination) noexcept -> void;

private:
    enum class SavedKind { Value, Place, StoredValue, StoredPlace, Success };

    struct Saved final {
        TargetIdentifier name;
        SavedKind kind;
    };

    struct Recipe final {
        const SemanticExpression* expression;
        std::vector<PreparedOperand> inputs;
        std::vector<Recipe*> operands;
        std::variant<std::monostate, Saved, TargetExpr, LoweringCompleted> completion;
    };

    // This chain borrows pending operations in suspended build() invocations.
    // It ends before those invocations complete and never enters recipe storage.
    struct PendingOperation final {
        PendingOperation* previous;
        Recipe& recipe;
        std::size_t postfix_end;
        bool direct_scalars;
        std::size_t postfix_cursor;
        std::vector<std::size_t> effects;
        std::vector<std::size_t> reads;
        std::size_t effect_cursor;
        std::size_t read_cursor;
    };

    std::deque<Recipe> recipes;
    BodyRealizer& owner;
    LifetimeRegionID cleanup;
    ExpressionBuilder* previous_frame;
    LoweringStmtBuilder declarations;
    LoweringStmtBuilder statements;

    auto take_statements(bool shared = false) noexcept -> LoweringStmtBuilder;

    template<typename T>
        requires (
            std::same_as<T, Saved>
            || std::same_as<T, TargetExpr>
            || std::same_as<T, LoweringCompleted>
        )
    static auto complete(Recipe& recipe, T result) noexcept -> void {
        if (!std::holds_alternative<std::monostate>(recipe.completion)
            && !std::holds_alternative<TargetExpr>(recipe.completion)) {
            invariant_violation("recipe completed more than once");
        }
        recipe.completion = std::move(result);
    }

    auto saved(const Recipe& recipe) const noexcept -> const Saved*;
    auto source(const Recipe& recipe) const noexcept -> const PreparedOperation&;
    auto scalar(TypeID id) const noexcept -> bool;
    auto names_storage(const PreparedOperation& value) const noexcept -> bool;
    auto borrowed_owner(const PreparedOperation& value, PreparedUse use) const noexcept -> bool;
    auto pending(const Recipe& recipe) const noexcept -> bool;
    auto discard_pending(Recipe& recipe) noexcept -> ContinuationTask<std::monostate>;
    auto has_storage_read(const Recipe& recipe) const noexcept -> bool;
    auto has_effect(const Recipe& recipe) const noexcept -> bool;
    auto commit_postfix(PendingOperation& operation) noexcept -> ContinuationTask<std::monostate>;
    auto commit_predecessors(
        PendingOperation& operation,
        bool include_reads,
        bool prefix_ready = false,
        std::size_t before = std::numeric_limits<std::size_t>::max()
    ) noexcept -> ContinuationTask<std::monostate>;
    auto flush_pending(PendingOperation* operation) noexcept -> ContinuationTask<std::monostate>;
    auto build(
        const SemanticExpression& expression,
        PendingOperation* pending,
        bool result_needed = true,
        PreparedUse result_use = PreparedUse::Consume,
        bool full_expression_root = false,
        bool propagate_outcome = false
    ) noexcept -> ContinuationTask<Recipe*>;
    auto complete_writer(
        Recipe& recipe,
        const SemFormat& format,
        const PreparedWriterFormat& preparation,
        std::optional<TargetIdentifier> output
    ) noexcept -> ContinuationTask<std::monostate>;
    auto sequenced_suffix_begin(const PreparedOperation& value) const noexcept -> std::size_t;
    auto first_unsequenced(const PreparedOperation& value) const noexcept -> std::size_t;
    auto preserve_borrows(Recipe& recipe) noexcept -> ContinuationTask<std::monostate>;
    auto raw(
        Recipe& recipe,
        ConstantLiteralContext literal = ConstantLiteralContext::Exact
    ) noexcept -> ContinuationTask<TargetExpr>;
    auto emit(
        Recipe& recipe,
        PreparedUse use,
        ConstantLiteralContext literal = ConstantLiteralContext::Exact
    ) noexcept -> ContinuationTask<TargetExpr>;
    auto anchor(
        Recipe& recipe,
        PreparedUse use,
        bool force = false,
        bool direct_scalar = false
    ) noexcept -> ContinuationTask<std::monostate>;
    auto complete_call(
        Recipe& recipe,
        const FallibleCall& transport,
        bool project_success,
        PreparedUse use,
        bool direct,
        bool propagate_outcome
    ) noexcept -> ContinuationTask<std::monostate>;
};
