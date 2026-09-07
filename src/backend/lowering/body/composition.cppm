module carven:backend.lowering.body.composition;

import :backend.target.expr;
import :backend.target.ids;
import :backend.target.name;
import :backend.target.origin;
import :backend.target.stmt;
import :support.invariant;
import std;

namespace body_lowering {

struct Unit final {};

enum class ExitKind { FunctionReturn, Failure, Break, Continue, Test, Value, Unreachable };

struct ExitTarget final {
    ExitKind kind;
    std::size_t identity;
    auto operator==(const ExitTarget&) const noexcept -> bool = default;
};

struct ExitSummary final {
    std::vector<ExitTarget> targets;
    auto contains(ExitTarget target) const noexcept -> bool {
        return std::ranges::contains(targets, target);
    }
    auto add(ExitTarget target) noexcept -> void {
        if (!contains(target)) {
            targets.push_back(target);
        }
    }
    auto merge(const ExitSummary& other) noexcept -> void {
        for (const auto target : other.targets) {
            add(target);
        }
    }
    auto consume(ExitTarget target) noexcept -> bool { return std::erase(targets, target) != 0; }
};

template<typename T>
struct Lowered final {
    std::vector<TargetStmt> statements;
    std::optional<T> normal;
    ExitSummary exits;
    bool has_declarations;
};

struct KnownBool final {
    bool value;
};

struct VoidResult final {};

struct DirectValue final {
    TargetExpr expression;
};

enum class ValueUse { Observe, Transfer };

struct OwnedValue final {
    TargetExpr storage;
};

struct TemporaryValue final {
    TargetExpr storage;
};

using Evaluated = std::variant<VoidResult, KnownBool, DirectValue, OwnedValue, TemporaryValue>;

auto value_expression(Evaluated value, ValueUse use = ValueUse::Transfer) noexcept -> TargetExpr;

struct DynamicBool final {
    TargetExpr expression;
};

using Predicate = std::variant<KnownBool, DynamicBool>;

auto known_predicate(const std::optional<Predicate>& predicate) noexcept -> std::optional<bool> {
    if (predicate) {
        if (const auto* known = std::get_if<KnownBool>(&*predicate)) {
            return known->value;
        }
    }
    return std::nullopt;
}

auto predicate_expression(Predicate predicate) noexcept -> TargetExpr;


class StatementBuilder;
using ValueConsumer = std::function<void(Evaluated, StatementBuilder&)>;

struct ConsumeResult final {
    ValueConsumer consume;
};

struct DiscardResult final {};

struct ReturnResult final {};

struct YieldResult final {
    ExitTarget target;
};

struct DeferredStorage final {
    TargetIdentifier name;
    TargetTypeID value_type;
};

struct InitializeResult final {
    DeferredStorage storage;
};
using ResultDestination =
    std::variant<DiscardResult, ReturnResult, YieldResult, InitializeResult, ConsumeResult>;

auto returns_result(const ResultDestination& result) noexcept -> bool {
    return std::holds_alternative<ReturnResult>(result)
        || std::holds_alternative<YieldResult>(result);
}

class StatementBuilder final {
public:
    StatementBuilder() noexcept
        : lowered {.statements = {}, .normal = Unit {}, .exits = {}, .has_declarations = false} {}

    auto continues() const noexcept -> bool { return lowered.normal.has_value(); }
    auto empty() const noexcept -> bool { return lowered.statements.empty(); }
    auto owns_storage() const noexcept -> bool { return lowered.has_declarations; }
    auto exits() const noexcept -> const ExitSummary& { return lowered.exits; }
    auto record_exits(const ExitSummary& exits) noexcept -> void { lowered.exits.merge(exits); }
    auto consume_exit(ExitTarget target) noexcept -> bool { return lowered.exits.consume(target); }
    auto emit(TargetStmt statement, bool continues = true) noexcept -> void;
    auto terminate(TargetStmt statement, ExitTarget target) noexcept -> void;
    auto append(StatementBuilder source) noexcept -> void;
    auto attribute(const TargetAttribution& attribution) noexcept -> void;
    auto scope(
        StatementBuilder source,
        TargetAttribution attribution = TargetGeneratedExpansionAttribution {
            .reason = TargetExpansionReason::LoweringSupport
        }
    ) noexcept -> void;
    auto resume(TargetIdentifier label, TargetJumpRole role, ExitTarget target) noexcept -> void;
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
        auto statements = StatementBuilder();
        statements.lowered.statements = std::move(source.statements);
        statements.lowered.normal = source.normal ? std::optional(Unit {}) : std::nullopt;
        statements.lowered.exits = std::move(source.exits);
        statements.lowered.has_declarations = source.has_declarations;
        append(std::move(statements));
        return std::move(source.normal);
    }
    auto result_region(TargetTypeID type, ExitTarget yield) && noexcept -> TargetExpr;
    auto finish() && noexcept -> std::vector<TargetStmt> { return std::move(lowered.statements); }

private:
    Lowered<Unit> lowered;
};

} // namespace body_lowering
