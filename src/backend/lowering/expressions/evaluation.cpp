module carven:backend.lowering.expressions.evaluation.impl;

import :backend.lowering.program;
import :backend.lowering.expressions;
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

auto intersects(
    std::span<const SemanticPlaceID> left,
    std::span<const SemanticPlaceID> right
) noexcept -> bool {
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
    std::span<const SemanticPlaceID> mutations,
    const EvaluationEffect& accesses
) noexcept -> bool {
    return intersects(mutations, accesses.reads)
        || intersects(mutations, accesses.writes)
        || intersects(mutations, accesses.takes);
}

auto empty_effect(const EvaluationEffect& effect) noexcept -> bool {
    return effect.reads.empty()
        && effect.writes.empty()
        && effect.takes.empty()
        && !effect.opaque_boundary
        && !effect.may_terminate;
}

auto observational_read(const EvaluationEffect& effect) noexcept -> bool {
    return effect.writes.empty()
        && effect.takes.empty()
        && !effect.opaque_boundary
        && !effect.may_terminate;
}

} // namespace

auto TargetEvaluationSequencer::effects_commute(
    const EvaluationEffect& left,
    const EvaluationEffect& right
) noexcept -> bool {
    if (conflicts_with_accesses(left.writes, right)
        || conflicts_with_accesses(left.takes, right)
        || conflicts_with_accesses(right.writes, left)
        || conflicts_with_accesses(right.takes, left)) {
        return false;
    }
    if (left.opaque_boundary || right.opaque_boundary) {
        return empty_effect(left) || empty_effect(right);
    }
    if (left.may_terminate) {
        return observational_read(right);
    }
    if (right.may_terminate) {
        return observational_read(left);
    }
    return true;
}

auto TargetEvaluationSequencer::stable_place_source(
    const TargetCallableLowerer& context,
    HIRExprID id
) noexcept -> bool {
    const auto& expression = context.semantic().expression(id).value;
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
    const auto name = context.fresh_name(TargetTemporaryNameKind::Operand);
    const auto binding = [&]() noexcept {
        switch (materialization) {
            case MaterializationKind::ReadValue:     return TargetVariableBinding::ConstValue;
            case MaterializationKind::ReadReference: return TargetVariableBinding::ConstReference;
            case MaterializationKind::WriteReference:
                return TargetVariableBinding::MutableReference;
            case MaterializationKind::Take:
            case MaterializationKind::Preserve: return TargetVariableBinding::RvalueReference;
            case MaterializationKind::Snapshot: return TargetVariableBinding::MutableValue;
        }
        std::unreachable();
    }();
    expression.prelude.push_back(context.target().append_statement({
        .value =
            TargetVariableStmt {
                .binding = binding,
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
    if (materialization == MaterializationKind::Preserve) {
        expression.expression = context.target().append_expression({
            .value = TargetForwardExpr {.name = name},
        });
    } else if (materialization == MaterializationKind::Take
               || materialization == MaterializationKind::Snapshot) {
        expression.expression = call_expression(
            context,
            name_expression(context, TargetSymbol::StdMove),
            {name_expression(context, TargetName {name})}
        );
    } else {
        expression.expression = name_expression(context, TargetName {name});
    }
    return expression;
}

auto TargetEvaluationSequencer::read_materialization(
    const TargetCallableLowerer& context,
    HIRTypeID type
) noexcept -> MaterializationKind {
    return context.read_parameter_by_value(type) ? MaterializationKind::ReadValue
                                                 : MaterializationKind::ReadReference;
}
