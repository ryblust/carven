module carven:backend.realization.expr;

import :backend.construction;
import :backend.lowering.constant;
import :backend.lowering.context;
import :backend.realization.operation;
import :backend.realization.realizer;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.type;
import :semantic.semir;
import :support.invariant;
import std;

// Execution buffers may nest while declarations remain with the source cleanup
// frame identified by LifetimeRegionID.
class BodyRealizer::ExpressionBuilder final {
public:
    ExpressionBuilder(BodyRealizer& owner, ConstructionExpressionID source) noexcept;

    ~ExpressionBuilder() noexcept;
    auto owns(ConstructionExpressionID source) const noexcept -> bool;
    auto evaluate(
        ConstructionExpressionID source,
        ConstantLiteralContext literal,
        ResultDemand demand,
        ConstructionUse use
    ) noexcept -> Lowered<LoweringResult>;
    auto deliver_structured(
        ConstructionExpressionID source,
        const LoweringResultDestination& result,
        LoweringStmtBuilder& destination,
        bool shared
    ) noexcept -> void;
    auto finish(
        ConstructionExpressionID source,
        ConstantLiteralContext literal,
        ResultDemand demand,
        ConstructionUse final_use = ConstructionUse::Consume,
        bool shared = false
    ) noexcept -> Lowered<LoweringResult>;
    auto initialize(
        const ConstructionInitialize& initialization,
        LoweringStmtBuilder& destination
    ) noexcept -> void;
    auto assign(const ConstructionAssign& assignment, LoweringStmtBuilder& destination) noexcept
        -> void;

private:
    enum class SavedKind { Value, Place, StoredValue, StoredPlace, Success };

    struct Saved final {
        TargetIdentifier name;
        SavedKind kind;
    };

    struct Recipe final {
        ConstructionExpressionID expression_id;
        std::span<const ConstructionOperand> inputs;
        std::vector<Recipe> operands;
        std::variant<std::monostate, Saved, TargetExpr, LoweringCompleted> completion;
    };

    // This synchronous chain only borrows live build() stack frames. It is not
    // retained in recipes or across source execution-region construction.
    struct PendingOperation final {
        PendingOperation* previous;
        Recipe& recipe;
        std::size_t postfix_end;
        bool direct_scalars;
        std::size_t postfix_cursor = 0;
        std::vector<std::size_t> effects;
        std::vector<std::size_t> reads;
        std::size_t effect_cursor = 0;
        std::size_t read_cursor = 0;
    };

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
    auto source(const Recipe& recipe) const noexcept -> const ConstructionExpression&;
    auto scalar(TypeID id) const noexcept -> bool;
    auto names_storage(const ConstructionExpression& value) const noexcept -> bool;
    auto pending(const Recipe& recipe) const noexcept -> bool;
    auto discard_pending(Recipe& recipe) noexcept -> void;
    auto has_storage_read(const Recipe& recipe) const noexcept -> bool;
    auto has_effect(const Recipe& recipe) const noexcept -> bool;
    auto commit_postfix(PendingOperation& operation) noexcept -> void;
    auto commit_predecessors(
        PendingOperation& operation,
        bool include_reads,
        bool prefix_ready = false
    ) noexcept -> void;
    auto flush_pending(PendingOperation* operation) noexcept -> void;
    auto build(
        ConstructionExpressionID id,
        PendingOperation* pending,
        bool result_needed = true,
        ConstructionUse result_use = ConstructionUse::Consume,
        bool full_expression_root = false,
        bool propagate_outcome = false
    ) noexcept -> Recipe;
    auto unordered(const ConstructionExpression& value) const noexcept -> bool;
    auto first_unsequenced(const ConstructionExpression& value) const noexcept -> std::size_t;
    auto preserve_borrows(Recipe& recipe) noexcept -> void;
    auto raw(
        Recipe& recipe,
        ConstantLiteralContext literal = ConstantLiteralContext::Exact
    ) noexcept -> TargetExpr;
    auto emit(
        Recipe& recipe,
        ConstructionUse use,
        ConstantLiteralContext literal = ConstantLiteralContext::Exact
    ) noexcept -> TargetExpr;
    auto anchor(
        Recipe& recipe,
        ConstructionUse use,
        bool force = false,
        bool direct_scalar = false
    ) noexcept -> void;
    auto complete_call(
        Recipe& recipe,
        const ConstructionFallible& transport,
        bool project_success,
        ConstructionUse use,
        bool direct,
        bool propagate_outcome
    ) noexcept -> void;
};
