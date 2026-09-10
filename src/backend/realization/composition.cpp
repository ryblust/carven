module carven:backend.realization.composition.impl;

import :backend.realization.composition;
import :backend.target;
import :backend.target.expr;
import :backend.target.origin;
import :backend.target.stmt;
import :support.invariant;
import :support.unique_indirect;
import std;

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
    lowered.has_declarations |= std::holds_alternative<TargetVariableStmt>(statement.value);
    lowered.statements.push_back(std::move(statement));
    if (!continues) {
        lowered.normal.reset();
    }
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
    lowered.statements.insert(
        lowered.statements.end(),
        std::make_move_iterator(source.lowered.statements.begin()),
        std::make_move_iterator(source.lowered.statements.end())
    );
    lowered.normal = source.lowered.normal;
    lowered.exits.merge(source.lowered.exits);
    lowered.has_declarations |= source.lowered.has_declarations;
}

auto LoweringStmtBuilder::attribute(const TargetAttribution& attribution) noexcept -> void {
    for (auto& statement : lowered.statements) {
        if (std::holds_alternative<TargetGeneratedExpansionAttribution>(statement.attribution)) {
            statement.attribution = attribution;
        }
    }
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
    if (continues()) {
        invariant_violation("value region has an undelivered normal result");
    }
    for (const auto target : exits().targets) {
        if (target != yield && target.kind != LoweringExitKind::Unreachable) {
            invariant_violation("value region contains an external control exit");
        }
    }
    return TargetExpr {
        .value = TargetLambdaExpr {
            .parameters = {},
            .result = type,
            .body = std::move(lowered.statements)
        }
    };
}

auto LoweringStmtBuilder::result_region(TargetTypeID type, LoweringExitTarget yield) && noexcept
    -> TargetExpr {
    return TargetExpr {
        .value = TargetCallExpr {
            .callee = UniqueIndirect(std::move(*this).result_factory(type, yield)),
            .template_argument_type_ids = {},
            .arguments = {}
        }
    };
}
