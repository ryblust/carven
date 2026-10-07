module carven:backend.realization.composition;

import :backend.target.expr;
import :backend.target.ids;
import :backend.target.name;
import :backend.target.origin;
import :backend.target.stmt;
import :support.invariant;
import std;

struct LoweringCompleted final {};

enum class LoweringExitKind {
    FunctionReturn,
    Failure,
    Cancelled,
    Break,
    Continue,
    Test,
    Value,
    Unreachable
};

// How a consumer receives the result of a value region.
enum class LoweringRegionDelivery {
    // The region is invoked as a typed factory.
    Factory,
    // A direct expression is copied into typed storage or a by-value operand.
    Copied,
    // A direct expression may be bound by reference.
    Bound,
};

struct LoweringExitTarget final {
    LoweringExitKind kind;
    std::size_t identity;
    auto operator==(const LoweringExitTarget&) const noexcept -> bool = default;
};

struct LoweringExit final {
    LoweringExitTarget target;
    bool needs_cleanup;
};

struct LoweringExitSummary final {
    std::vector<LoweringExit> entries;

    auto contains(LoweringExitTarget target) const noexcept -> bool;
    auto crosses_cleanup(LoweringExitTarget target) const noexcept -> bool;
    auto add(LoweringExitTarget target, bool needs_cleanup = false) noexcept -> void;
    auto merge(const LoweringExitSummary& other, bool needs_cleanup = false) noexcept -> void;
    auto consume(LoweringExitTarget target) noexcept -> bool;
};

// Statement fragments concatenate before any physical C++ region is finished.
// Chunks preserve O(1) composition without a heap allocation per statement.
class LoweringStatements final {
public:
    LoweringStatements() = default;
    LoweringStatements(const LoweringStatements&) = delete;

    LoweringStatements(LoweringStatements&& source) noexcept;

    auto operator=(const LoweringStatements&) -> LoweringStatements& = delete;

    auto operator=(LoweringStatements&& source) noexcept -> LoweringStatements&;

    auto empty() const noexcept -> bool;

    auto push_back(TargetStmt statement) noexcept -> void;

    auto append(LoweringStatements source) noexcept -> void;

    template<typename Visitor>
    auto visit(const Visitor& visitor) noexcept -> void {
        for (auto& chunk : chunks) {
            for (auto& statement : chunk) {
                visitor(statement);
            }
        }
    }

    auto finish() && noexcept -> std::vector<TargetStmt>;

private:
    std::list<std::vector<TargetStmt>> chunks;
    std::size_t count = 0;
};

template<typename T>
struct Lowered final {
    LoweringStatements statements;
    std::optional<T> normal;
    LoweringExitSummary exits;
    bool has_declarations;
    // Cleanup still owed by this statement scope; closed child scopes have
    // already discharged their own objects before a successor executes.
    bool needs_cleanup;
};

struct LoweringKnownBool final {
    bool value;
};

struct LoweringDirectExpression final {
    TargetExpr expression;
};

using LoweringResult = std::variant<LoweringCompleted, LoweringDirectExpression>;

auto remaining_expression(LoweringResult result) noexcept -> std::optional<TargetExpr>;

auto require_expression(LoweringResult value) noexcept -> TargetExpr;

struct LoweringDynamicBool final {
    TargetExpr expression;
};

using LoweringPredicate = std::variant<LoweringKnownBool, LoweringDynamicBool>;

auto known_predicate(const std::optional<LoweringPredicate>& predicate) noexcept
    -> std::optional<bool> {
    if (predicate) {
        if (const auto* known = std::get_if<LoweringKnownBool>(&*predicate)) {
            return known->value;
        }
    }
    return std::nullopt;
}

auto predicate_expression(LoweringPredicate predicate) noexcept -> TargetExpr;

struct LoweringDiscardResult final {};

struct LoweringReturnResult final {};

struct LoweringYieldResult final {
    LoweringExitTarget target;
};

struct LoweringDeferredStorage final {
    TargetLocalID local;
    TargetTypeID value_type;
};

struct LoweringInitializeResult final {
    LoweringDeferredStorage storage;
};

// Each arm assigns its result to a named local.
struct LoweringAssignResult final {
    TargetLocalID local;
};

using LoweringResultDestination = std::variant<
    LoweringDiscardResult,
    LoweringReturnResult,
    LoweringYieldResult,
    LoweringInitializeResult,
    LoweringAssignResult>;

auto returns_result(const LoweringResultDestination& result) noexcept -> bool {
    return std::holds_alternative<LoweringReturnResult>(result)
        || std::holds_alternative<LoweringYieldResult>(result);
}

class LoweringStmtBuilder final {
public:
    LoweringStmtBuilder() noexcept;
    auto continues() const noexcept -> bool;
    auto empty() const noexcept -> bool;
    auto owns_storage() const noexcept -> bool;
    auto needs_cleanup() const noexcept -> bool;
    auto exits() const noexcept -> const LoweringExitSummary&;
    auto record_exits(const LoweringExitSummary& exits) noexcept -> void;
    auto consume_exit(LoweringExitTarget target) noexcept -> bool;
    auto replace_exit(LoweringExitTarget target, const LoweringExitSummary& continuation) noexcept
        -> void;
    auto emit(TargetStmt statement, bool continues = true) noexcept -> void;
    auto declare(TargetVariableStmt variable, bool needs_cleanup) noexcept -> void;
    auto terminate(TargetStmt statement, LoweringExitTarget target) noexcept -> void;
    auto append(LoweringStmtBuilder source) noexcept -> void;
    auto attribute(const TargetAttribution& attribution) noexcept -> void;
    auto scope(
        LoweringStmtBuilder source,
        TargetAttribution attribution = TargetGeneratedExpansionAttribution {
            .reason = TargetExpansionReason::LoweringSupport
        }
    ) noexcept -> void;
    auto resume(TargetIdentifier label, TargetJumpRole role, LoweringExitTarget target) noexcept
        -> void;

    template<typename T>
    auto complete(std::optional<T> value) && noexcept -> Lowered<T> {
        if (continues() != value.has_value()) {
            invariant_violation("evaluation result disagrees with normal completion");
        }
        return {
            .statements = std::move(lowered.statements),
            .normal = std::move(value),
            .exits = std::move(lowered.exits),
            .has_declarations = lowered.has_declarations,
            .needs_cleanup = lowered.needs_cleanup,
        };
    }

    template<typename T>
    auto accept(Lowered<T> source) noexcept -> std::optional<T> {
        if (!continues()) {
            return std::nullopt;
        }
        auto statements = LoweringStmtBuilder();
        statements.lowered.statements = std::move(source.statements);
        statements.lowered.normal =
            source.normal ? std::optional(LoweringCompleted {}) : std::nullopt;
        statements.lowered.exits = std::move(source.exits);
        statements.lowered.has_declarations = source.has_declarations;
        statements.lowered.needs_cleanup = source.needs_cleanup;
        append(std::move(statements));
        return std::move(source.normal);
    }

    auto result_factory(TargetTypeID type, LoweringExitTarget yield) && noexcept -> TargetExpr;
    // A direct region that only returns expressions is spelled as that
    // expression, or as `?:` for one two-way conditional; any other region
    // invokes a typed factory. A copied result relies on its typed consumer. A
    // bound result is cast to the result type, so a selected expression that
    // names storage still delivers a value.
    auto result_region(
        TargetTypeID type,
        LoweringExitTarget yield,
        LoweringRegionDelivery delivery
    ) && noexcept -> TargetExpr;
    auto finish() && noexcept -> std::vector<TargetStmt>;

    template<typename Visitor>
    auto visit_statements(const Visitor& visitor) noexcept -> void {
        lowered.statements.visit(visitor);
    }

private:
    Lowered<LoweringCompleted> lowered;
};
