module carven:backend.lowering.body.composition;

import :backend.target.expr;
import :backend.target.ids;
import :backend.target.name;
import :backend.target.origin;
import :backend.target.stmt;
import :support.invariant;
import std;

struct LoweringUnit final {};

enum class LoweringExitKind { FunctionReturn, Failure, Break, Continue, Test, Value, Unreachable };

struct LoweringExitTarget final {
    LoweringExitKind kind;
    std::size_t identity;
    auto operator==(const LoweringExitTarget&) const noexcept -> bool = default;
};

struct LoweringExitSummary final {
    std::vector<LoweringExitTarget> targets;

    auto contains(LoweringExitTarget target) const noexcept -> bool {
        return std::ranges::contains(targets, target);
    }

    auto add(LoweringExitTarget target) noexcept -> void {
        if (!contains(target)) {
            targets.push_back(target);
        }
    }

    auto merge(const LoweringExitSummary& other) noexcept -> void {
        for (const auto target : other.targets) {
            add(target);
        }
    }

    auto consume(LoweringExitTarget target) noexcept -> bool {
        return std::erase(targets, target) != 0;
    }
};

template<typename T>
struct Lowered final {
    std::vector<TargetStmt> statements;
    std::optional<T> normal;
    LoweringExitSummary exits;
    bool has_declarations;
};

struct LoweringKnownBool final {
    bool value;
};

struct LoweringVoidResult final {};

struct LoweringDirectValue final {
    TargetExpr expression;
};

enum class LoweringValueUse { Observe, Transfer };

struct LoweringOwnedValue final {
    TargetExpr storage;
};

struct LoweringTemporaryValue final {
    TargetExpr storage;
};

using LoweringValue = std::variant<
    LoweringVoidResult,
    LoweringKnownBool,
    LoweringDirectValue,
    LoweringOwnedValue,
    LoweringTemporaryValue>;

auto value_expression(
    LoweringValue value,
    LoweringValueUse use = LoweringValueUse::Transfer
) noexcept -> TargetExpr;

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

class LoweringStmtBuilder;
using LoweringValueConsumer = std::function<void(LoweringValue, LoweringStmtBuilder&)>;

struct LoweringConsumeResult final {
    LoweringValueConsumer consume;
};

struct LoweringDiscardResult final {};

struct LoweringReturnResult final {};

struct LoweringYieldResult final {
    LoweringExitTarget target;
};

struct LoweringDeferredStorage final {
    TargetIdentifier name;
    TargetTypeID value_type;
};

struct LoweringInitializeResult final {
    LoweringDeferredStorage storage;
};

using LoweringResultDestination = std::variant<
    LoweringDiscardResult,
    LoweringReturnResult,
    LoweringYieldResult,
    LoweringInitializeResult,
    LoweringConsumeResult>;

auto returns_result(const LoweringResultDestination& result) noexcept -> bool {
    return std::holds_alternative<LoweringReturnResult>(result)
        || std::holds_alternative<LoweringYieldResult>(result);
}

class LoweringStmtBuilder final {
public:
    LoweringStmtBuilder() noexcept
        : lowered {
              .statements = {},
              .normal = LoweringUnit {},
              .exits = {},
              .has_declarations = false
          } {}

    auto continues() const noexcept -> bool { return lowered.normal.has_value(); }

    auto empty() const noexcept -> bool { return lowered.statements.empty(); }

    auto owns_storage() const noexcept -> bool { return lowered.has_declarations; }

    auto exits() const noexcept -> const LoweringExitSummary& { return lowered.exits; }

    auto record_exits(const LoweringExitSummary& exits) noexcept -> void {
        lowered.exits.merge(exits);
    }

    auto consume_exit(LoweringExitTarget target) noexcept -> bool {
        return lowered.exits.consume(target);
    }

    auto emit(TargetStmt statement, bool continues = true) noexcept -> void;
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
        };
    }

    template<typename T>
    auto accept(Lowered<T> source) noexcept -> std::optional<T> {
        if (!continues()) {
            return std::nullopt;
        }
        auto statements = LoweringStmtBuilder();
        statements.lowered.statements = std::move(source.statements);
        statements.lowered.normal = source.normal ? std::optional(LoweringUnit {}) : std::nullopt;
        statements.lowered.exits = std::move(source.exits);
        statements.lowered.has_declarations = source.has_declarations;
        append(std::move(statements));
        return std::move(source.normal);
    }

    auto result_region(TargetTypeID type, LoweringExitTarget yield) && noexcept -> TargetExpr;

    auto finish() && noexcept -> std::vector<TargetStmt> { return std::move(lowered.statements); }

private:
    Lowered<LoweringUnit> lowered;
};
