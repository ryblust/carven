module carven:backend.lowering.expr.evaluation.impl;

import :backend.lowering.program;
import :backend.lowering.expr;
import :backend.generation.names;
import :backend.lowering.names;
import :backend.lowering.types;
import :backend.target;
import :backend.target.expr;
import :backend.target.stmt;
import :semantic.hir.expr;
import :semantic.hir.place;
import std;

namespace {

auto intersects(std::span<const SymbolID> left, std::span<const SymbolID> right) noexcept -> bool {
    auto left_index = 0uz;
    auto right_index = 0uz;
    while (left_index < left.size() && right_index < right.size()) {
        const auto left_id = left[left_index].index();
        const auto right_id = right[right_index].index();
        if (left_id < right_id) {
            ++left_index;
        } else if (right_id < left_id) {
            ++right_index;
        } else {
            return true;
        }
    }
    return false;
}

auto conflicts_with_accesses(
    std::span<const SymbolID> mutations,
    const EvaluationEffect& accesses
) noexcept -> bool {
    return intersects(mutations, accesses.reads)
        || intersects(mutations, accesses.writes)
        || intersects(mutations, accesses.takes);
}

auto empty_effect(const EvaluationEffect& effect, bool may_terminate) noexcept -> bool {
    return effect.reads.empty()
        && effect.writes.empty()
        && effect.takes.empty()
        && !effect.opaque_boundary
        && !may_terminate;
}

auto observational_read(const EvaluationEffect& effect, bool may_terminate) noexcept -> bool {
    return effect.writes.empty()
        && effect.takes.empty()
        && !effect.opaque_boundary
        && !may_terminate;
}

auto materialization_binding(MaterializationKind materialization) noexcept
    -> TargetVariableBinding {
    switch (materialization) {
        case MaterializationKind::ReadValue:      return TargetVariableBinding::ConstValue;
        case MaterializationKind::ReadReference:  return TargetVariableBinding::ConstReference;
        case MaterializationKind::WriteReference: return TargetVariableBinding::MutableReference;
        case MaterializationKind::Take:
        case MaterializationKind::Preserve:       return TargetVariableBinding::RvalueReference;
        case MaterializationKind::Snapshot:       return TargetVariableBinding::MutableValue;
    }
    std::unreachable();
}

auto materialize_expression_name(
    TargetCallableLowerer& context,
    LoweredExpression expression,
    MaterializationKind materialization,
    TargetMaterializationReason reason
) noexcept -> MaterializedExpressionName {
    const auto name = context.fresh_name(TargetTemporaryNameKind::Operand);
    expression.prelude.push_back(context.target().append_statement({
        .value =
            TargetVariableStmt {
                .binding = materialization_binding(materialization),
                .name = name,
                .type = intrinsic_type(context, TargetSymbol::Auto),
                .initializer = expression.expression,
                .maybe_unused = false,
            },
        .attribution = {
            .kind = TargetAttributionKind::SourceExpansion,
            .origin = std::nullopt,
            .reason = reason == TargetMaterializationReason::FailureTransport
                ? TargetSyntheticReason::FailureTransport
                : TargetSyntheticReason::EvaluationOrder,
        },
    }));
    return {
        .prelude_ids = std::move(expression.prelude),
        .name = name,
        .unconsumed_carrier = std::move(expression.unconsumed_carrier),
    };
}

} // namespace

auto TargetEvaluationSequencer::effects_commute(
    const EvaluationEffect& left,
    bool left_may_terminate,
    const EvaluationEffect& right,
    bool right_may_terminate
) noexcept -> bool {
    if (conflicts_with_accesses(left.writes, right)
        || conflicts_with_accesses(left.takes, right)
        || conflicts_with_accesses(right.writes, left)
        || conflicts_with_accesses(right.takes, left)) {
        return false;
    }
    if (left.opaque_boundary || right.opaque_boundary) {
        return empty_effect(left, left_may_terminate) || empty_effect(right, right_may_terminate);
    }
    if (left_may_terminate) {
        return observational_read(right, right_may_terminate);
    }
    if (right_may_terminate) {
        return observational_read(left, left_may_terminate);
    }
    return true;
}

auto TargetEvaluationSequencer::expressions_commute(
    const TargetCallableLowerer& context,
    HIRExprID left,
    HIRExprID right
) noexcept -> bool {
    const auto may_terminate = [&](HIRExprID expression) noexcept {
        const auto& control = context.source().expression_control(expression);
        return control.exits_test
            || !context.failure_profile(control.evaluation_failure_set).ordered_members.empty();
    };
    return effects_commute(
        context.source().evaluation_effect(left),
        may_terminate(left),
        context.source().evaluation_effect(right),
        may_terminate(right)
    );
}

auto TargetEvaluationSequencer::stable_place_source(
    const TargetCallableLowerer& context,
    HIRExprID id
) noexcept -> bool {
    const auto& expression = context.source().expression(id).value;
    if (std::holds_alternative<HIRNameExpr>(expression)) {
        return true;
    }
    if (const auto* cast = std::get_if<HIRCastExpr>(&expression)) {
        return cast->kind == HIRCastKind::Identity
            && stable_place_source(context, cast->operand_id);
    }
    if (const auto* member = std::get_if<HIRMemberExpr>(&expression)) {
        return std::holds_alternative<HIRStructFieldTarget>(member->target)
            && stable_place_source(context, member->operand_id);
    }
    return false;
}

auto cpp_bool_cast(TargetCallableLowerer& context, TargetExprID expression) noexcept
    -> TargetExprID {
    return context.target().append_expression({
        .value = TargetStaticCastExpr {
            .type = intrinsic_type(context, TargetSymbol::Bool),
            .operand_id = expression,
        },
    });
}

auto TargetEvaluationSequencer::materialize(
    TargetCallableLowerer& context,
    LoweredExpression expression,
    MaterializationKind materialization,
    TargetMaterializationReason reason
) noexcept -> LoweredExpression {
    auto named =
        materialize_expression_name(context, std::move(expression), materialization, reason);
    const auto result_id = [&]() noexcept -> TargetExprID {
        if (materialization == MaterializationKind::Preserve) {
            return context.target().append_expression({
                .value = TargetForwardExpr {.name = named.name},
            });
        }
        if (materialization == MaterializationKind::Take
            || materialization == MaterializationKind::Snapshot) {
            return call_expression(
                context,
                name_expression(context, TargetSymbol::StdMove),
                {name_expression(context, TargetName {named.name})}
            );
        }
        return name_expression(context, TargetName {named.name});
    }();
    return {
        .prelude = std::move(named.prelude_ids),
        .expression = result_id,
        .unconsumed_carrier = std::move(named.unconsumed_carrier),
    };
}

auto TargetEvaluationSequencer::materialize_read_name(
    TargetCallableLowerer& context,
    LoweredExpression expression,
    HIRTypeID type_id,
    TargetMaterializationReason reason
) noexcept -> MaterializedExpressionName {
    return materialize_expression_name(
        context,
        std::move(expression),
        read_materialization(context, type_id),
        reason
    );
}

auto TargetEvaluationSequencer::read_materialization(
    const TargetCallableLowerer& context,
    HIRTypeID type
) noexcept -> MaterializationKind {
    return context.read_parameter_by_value(type) ? MaterializationKind::ReadValue
                                                 : MaterializationKind::ReadReference;
}
