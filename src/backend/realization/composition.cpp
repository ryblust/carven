module carven:backend.realization.composition.impl;

import :backend.realization.composition;
import :backend.target;
import :backend.target.builder;
import :backend.target.expr;
import :backend.target.ids;
import :backend.target.name;
import :backend.target.origin;
import :backend.target.stmt;
import :support.invariant;
import :support.unique_indirect;
import std;

namespace {

auto require_value_region(const LoweringStmtBuilder& region, LoweringExitTarget yield) noexcept
    -> void {
    if (region.continues()) {
        invariant_violation("value region has an undelivered normal result");
    }
    for (const auto target : region.exits().targets) {
        if (target != yield && target.kind != LoweringExitKind::Unreachable) {
            invariant_violation("value region contains an external control exit");
        }
    }
}

} // namespace

LoweringStatements::LoweringStatements(LoweringStatements&& source) noexcept
    : chunks(std::move(source.chunks)),
      count(std::exchange(source.count, 0)) {}

auto LoweringStatements::operator=(LoweringStatements&& source) noexcept -> LoweringStatements& {
    chunks = std::move(source.chunks);
    count = std::exchange(source.count, 0);
    return *this;
}

auto LoweringStatements::empty() const noexcept -> bool {
    return count == 0;
}

auto LoweringStatements::push_back(TargetStmt statement) noexcept -> void {
    if (chunks.empty()) {
        chunks.emplace_back();
    }
    chunks.back().push_back(std::move(statement));
    ++count;
}

auto LoweringStatements::append(LoweringStatements source) noexcept -> void {
    count += source.count;
    chunks.splice(chunks.end(), source.chunks);
    source.count = 0;
}

auto LoweringStatements::finish() && noexcept -> std::vector<TargetStmt> {
    auto result = std::vector<TargetStmt>();
    result.reserve(count);
    for (auto& chunk : chunks) {
        result.insert(
            result.end(),
            std::make_move_iterator(chunk.begin()),
            std::make_move_iterator(chunk.end())
        );
    }
    chunks.clear();
    count = 0;
    return result;
}

auto remaining_expression(LoweringResult result) noexcept -> std::optional<TargetExpr> {
    if (auto* expression = std::get_if<LoweringDirectExpression>(&result)) {
        return std::move(expression->expression);
    }
    return std::nullopt;
}

auto require_expression(LoweringResult value) noexcept -> TargetExpr {
    auto expression = remaining_expression(std::move(value));
    if (!expression) {
        invariant_violation("completed evaluation has no value expression");
    }
    return std::move(*expression);
}

auto predicate_expression(LoweringPredicate predicate) noexcept -> TargetExpr {
    if (const auto* known = std::get_if<LoweringKnownBool>(&predicate)) {
        return bool_expression(known->value);
    }
    return std::move(std::get<LoweringDynamicBool>(predicate).expression);
}

auto LoweringStmtBuilder::emit(TargetStmt statement, bool continues) noexcept -> void {
    if (!this->continues()) {
        return;
    }
    const auto declaration = std::holds_alternative<TargetVariableStmt>(statement.value);
    lowered.has_declarations |= declaration;
    // Unclassified declarations retain their cleanup boundary. Producers with
    // a representation proof use declare to state the actual obligation.
    lowered.needs_cleanup |= declaration;
    lowered.statements.push_back(std::move(statement));
    if (!continues) {
        lowered.normal.reset();
    }
}

auto LoweringStmtBuilder::declare(TargetVariableStmt variable, bool needs_cleanup) noexcept
    -> void {
    if (!continues()) {
        return;
    }
    lowered.has_declarations = true;
    lowered.needs_cleanup |= needs_cleanup;
    lowered.statements.push_back(target_lowering_statement(std::move(variable)));
}

auto LoweringStmtBuilder::terminate(TargetStmt statement, LoweringExitTarget target) noexcept
    -> void {
    if (!continues()) {
        return;
    }
    emit(std::move(statement), false);
    lowered.exits.add(target);
}

auto LoweringStmtBuilder::append(LoweringStmtBuilder source) noexcept -> void {
    if (!continues()) {
        return;
    }
    lowered.statements.append(std::move(source.lowered.statements));
    lowered.normal = source.lowered.normal;
    lowered.exits.merge(source.lowered.exits);
    lowered.has_declarations |= source.lowered.has_declarations;
    lowered.needs_cleanup |= source.lowered.needs_cleanup;
}

auto LoweringStmtBuilder::attribute(const TargetAttribution& attribution) noexcept -> void {
    lowered.statements.visit([&](TargetStmt& statement) noexcept {
        if (std::holds_alternative<TargetGeneratedExpansionAttribution>(statement.attribution)) {
            statement.attribution = attribution;
        }
    });
}

auto LoweringStmtBuilder::scope(LoweringStmtBuilder source, TargetAttribution attribution) noexcept
    -> void {
    if (!continues()) {
        return;
    }
    source.attribute(attribution);
    if (!source.owns_storage()) {
        append(std::move(source));
        return;
    }
    const auto normal = source.continues();
    lowered.exits.merge(source.exits());
    emit(
        TargetStmt {
            .value = TargetBlockStmt {.statements = std::move(source).finish()},
            .attribution = std::move(attribution)
        },
        normal
    );
}

auto LoweringStmtBuilder::resume(
    TargetIdentifier label,
    TargetJumpRole role,
    LoweringExitTarget target
) noexcept -> void {
    if (!consume_exit(target)) {
        invariant_violation("continuation does not own a pending exit");
    }
    lowered.normal = LoweringCompleted {};
    emit(
        TargetStmt {
            .value = TargetLabelStmt {.label = std::move(label), .role = role},
            .attribution = TargetGeneratedExpansionAttribution {
                .reason = TargetExpansionReason::LoweringSupport
            }
        }
    );
}

auto LoweringStmtBuilder::result_factory(TargetTypeID type, LoweringExitTarget yield) && noexcept
    -> TargetExpr {
    require_value_region(*this, yield);
    return TargetExpr {
        .value = TargetLambdaExpr {
            .parameters = {},
            .result = type,
            .body = std::move(lowered.statements).finish()
        }
    };
}

auto LoweringStmtBuilder::result_region(
    TargetTypeID type,
    LoweringExitTarget yield,
    LoweringRegionDelivery delivery
) && noexcept -> TargetExpr {
    require_value_region(*this, yield);
    const auto returned = [](std::vector<TargetStmt>& body) static noexcept -> TargetExpr* {
        auto* value =
            body.size() == 1uz ? std::get_if<TargetReturnStmt>(&body.front().value) : nullptr;
        return value != nullptr && value->expression ? &*value->expression : nullptr;
    };
    const auto bound = delivery == LoweringRegionDelivery::Bound;
    // An integer literal has no type of its own; a copied arm states it.
    const auto arm = [&](TargetExpr& value) noexcept -> TargetExpr {
        const auto* literal = std::get_if<TargetLiteralExpr>(&value.value);
        if (bound
            || literal == nullptr
            || !std::holds_alternative<TargetIntegerLiteral>(literal->value)) {
            return std::move(value);
        }
        auto initializer = std::vector<TargetExpr>();
        initializer.push_back(std::move(value));
        return {
            .value = TargetConstructionExpr {.type = type, .initializer = std::move(initializer)}
        };
    };
    const auto deliver = [&](TargetExpr value) noexcept -> TargetExpr {
        if (!bound) {
            return value;
        }
        return {
            .value =
                TargetStaticCastExpr {.type = type, .operand = UniqueIndirect(std::move(value))}
        };
    };
    if (delivery != LoweringRegionDelivery::Factory && !lowered.has_declarations) {
        auto statements = std::move(lowered.statements).finish();
        if (auto* value = returned(statements)) {
            return deliver(arm(*value));
        }
        auto* conditional = statements.size() == 1uz
            ? std::get_if<TargetIfStmt>(&statements.front().value)
            : nullptr;
        if (conditional != nullptr
            && conditional->branches.size() == 1uz
            && conditional->else_body) {
            auto* selected = returned(conditional->branches.front().body);
            auto* alternative = returned(*conditional->else_body);
            if (selected != nullptr && alternative != nullptr) {
                return deliver(
                    TargetExpr {
                        .value = TargetConditionalExpr {
                            .condition =
                                UniqueIndirect(std::move(conditional->branches.front().condition)),
                            .true_value = UniqueIndirect(arm(*selected)),
                            .false_value = UniqueIndirect(arm(*alternative)),
                        }
                    }
                );
            }
        }
        for (auto& statement : statements) {
            lowered.statements.push_back(std::move(statement));
        }
    }
    return TargetExpr {
        .value = TargetCallExpr {
            .callee = UniqueIndirect(std::move(*this).result_factory(type, yield)),
            .template_arguments = {},
            .arguments = {}
        }
    };
}

auto LoweringExitSummary::contains(LoweringExitTarget target) const noexcept -> bool {
    return std::ranges::contains(targets, target);
}

auto LoweringExitSummary::add(LoweringExitTarget target) noexcept -> void {
    if (!contains(target)) {
        targets.push_back(target);
    }
}

auto LoweringExitSummary::merge(const LoweringExitSummary& other) noexcept -> void {
    for (const auto target : other.targets) {
        add(target);
    }
}

auto LoweringExitSummary::consume(LoweringExitTarget target) noexcept -> bool {
    return std::erase(targets, target) != 0;
}

LoweringStmtBuilder::LoweringStmtBuilder() noexcept
    : lowered {
          .statements = {},
          .normal = LoweringCompleted {},
          .exits = {},
          .has_declarations = false,
          .needs_cleanup = false
      } {}

auto LoweringStmtBuilder::continues() const noexcept -> bool {
    return lowered.normal.has_value();
}

auto LoweringStmtBuilder::empty() const noexcept -> bool {
    return lowered.statements.empty();
}

auto LoweringStmtBuilder::owns_storage() const noexcept -> bool {
    return lowered.has_declarations;
}

auto LoweringStmtBuilder::needs_cleanup() const noexcept -> bool {
    return lowered.needs_cleanup;
}

auto LoweringStmtBuilder::exits() const noexcept -> const LoweringExitSummary& {
    return lowered.exits;
}

auto LoweringStmtBuilder::record_exits(const LoweringExitSummary& exits) noexcept -> void {
    lowered.exits.merge(exits);
}

auto LoweringStmtBuilder::consume_exit(LoweringExitTarget target) noexcept -> bool {
    return lowered.exits.consume(target);
}

auto LoweringStmtBuilder::finish() && noexcept -> std::vector<TargetStmt> {
    return std::move(lowered.statements).finish();
}
