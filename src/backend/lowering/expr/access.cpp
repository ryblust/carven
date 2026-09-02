module carven:backend.lowering.expr.access.impl;

import :backend.lowering.program;
import :backend.lowering.expr;
import :backend.generation.names;
import :backend.lowering.names;
import :backend.lowering.types;
import :backend.target;
import :backend.target.expr;
import :backend.target.stmt;
import :semantic.hir;
import :semantic.hir.expr;
import :semantic.hir.ids;
import :semantic.hir.symbol;
import :semantic.hir.type;
import :support.visit;
import std;

namespace {

auto uses_scope_access(const HIRMemberExpr& member) noexcept -> bool {
    return std::visit(
        Overloaded {
            [](const HIRUnresolvedMemberTarget& target) static noexcept { return target.scope; },
            [](const HIRStructFieldTarget&) static noexcept { return false; },
            [](const HIREnumCaseTarget&) static noexcept { return true; },
        },
        member.target
    );
}

auto requires_operand_sequencing(
    const TargetCallableLowerer& context,
    HIRExprID operand,
    HIRExprID index,
    const LoweredExpression& lowered_index
) noexcept -> bool {
    if (TargetEvaluationSequencer::stable_place_source(context, operand)) {
        return false;
    }
    return !lowered_index.prelude.empty()
        || !TargetEvaluationSequencer::expressions_commute(context, operand, index);
}

auto member_expression(
    TargetCallableLowerer& context,
    TargetExprID operand,
    const HIRMemberExpr& member
) noexcept -> TargetExprID {
    auto name = member_name(context, member);
    if (!uses_scope_access(member)) {
        return context.target().append_expression({
            .value = TargetMemberExpr {
                .operand_id = operand,
                .name = std::move(name),
            },
        });
    }
    return context.target().append_expression({
        .value = TargetScopeMemberExpr {
            .operand_id = operand,
            .name = std::move(name),
        },
    });
}

} // namespace

auto lower_mutation_expression(
    TargetCallableLowerer& context,
    HIRExprID id,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression {
    const auto& source = context.source().expression(id);
    if (const auto* name = std::get_if<HIRNameExpr>(&source.value)) {
        return {
            .prelude = {},
            .expression = name_expression(context, symbol_reference_name(context, name->symbol)),
        };
    }
    if (const auto* member = std::get_if<HIRMemberExpr>(&source.value)) {
        auto operand = lower_mutation_expression(context, member->operand_id, control);
        operand.expression = member_expression(context, operand.expression, *member);
        return operand;
    }
    const auto& index = std::get<HIRIndexExpr>(source.value);
    auto operand = lower_mutation_expression(context, index.operand_id, control);
    auto lowered_index = lower_expression(context, index.index, control);
    if (requires_operand_sequencing(context, index.operand_id, index.index, lowered_index)) {
        operand = TargetEvaluationSequencer::materialize(
            context,
            std::move(operand),
            MaterializationKind::WriteReference,
            TargetMaterializationReason::EvaluationOrder
        );
    }
    auto prelude = std::move(operand.prelude);
    prelude.insert(
        prelude.end(),
        std::make_move_iterator(lowered_index.prelude.begin()),
        std::make_move_iterator(lowered_index.prelude.end())
    );
    auto selected = std::optional<TargetExprID>();
    if (std::holds_alternative<HIRArrayTypeValue>(
            context.source().type(context.source().expression(index.operand_id).type).value
        )) {
        selected = call_expression(
            context,
            name_expression(context, TargetSymbol::RuntimeCheckedArrayIndex),
            {operand.expression, lowered_index.expression}
        );
    } else {
        selected = context.target().append_expression({
            .value = TargetIndexExpr {
                .operand_id = operand.expression,
                .index = lowered_index.expression,
            },
        });
    }
    return {.prelude = std::move(prelude), .expression = *selected};
}

auto lower_expression(
    TargetCallableLowerer& context,
    HIRExprID,
    const HIRTextIntrinsicExpr& expression,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression {
    auto operand = lower_expression(context, expression.operand_id, control);
    if (expression.intrinsic == HIRTextIntrinsic::Len
        || expression.intrinsic == HIRTextIntrinsic::IsEmpty) {
        const auto member = context.target().append_expression({
            .value = TargetMemberExpr {
                .operand_id = operand.expression,
                .name = TargetNameAllocator::fixed(
                    expression.intrinsic == HIRTextIntrinsic::Len ? "size" : "empty"
                )
            },
        });
        operand.expression = call_expression(context, member, {});
        return operand;
    }
    operand.expression = call_expression(
        context,
        name_expression(
            context,
            expression.intrinsic == HIRTextIntrinsic::Bytes ? TargetSymbol::RuntimeStrBytes
                                                            : TargetSymbol::RuntimeStrChars
        ),
        {operand.expression}
    );
    return operand;
}

auto lower_expression(
    TargetCallableLowerer& context,
    HIRExprID,
    const HIRIndexExpr& expression,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression {
    auto operand = lower_expression(context, expression.operand_id, control);
    auto index = lower_expression(context, expression.index, control);
    if (requires_operand_sequencing(context, expression.operand_id, expression.index, index)) {
        operand = TargetEvaluationSequencer::materialize(
            context,
            std::move(operand),
            TargetEvaluationSequencer::read_materialization(
                context,
                context.source().expression(expression.operand_id).type
            ),
            TargetMaterializationReason::EvaluationOrder
        );
    }
    auto prelude = std::move(operand.prelude);
    prelude.insert(
        prelude.end(),
        std::make_move_iterator(index.prelude.begin()),
        std::make_move_iterator(index.prelude.end())
    );
    const auto selected = call_expression(
        context,
        name_expression(context, TargetSymbol::RuntimeCheckedArrayIndex),
        {operand.expression, index.expression}
    );
    return {.prelude = std::move(prelude), .expression = selected};
}

auto lower_expression(
    TargetCallableLowerer& context,
    HIRExprID id,
    const HIRMemberExpr& expression,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression {
    if (const auto constant = context.source().expression(id).constant) {
        return {.prelude = {}, .expression = lower_constant_value(context, *constant)};
    }
    auto operand = lower_expression(context, expression.operand_id, control);
    operand.expression = member_expression(context, operand.expression, expression);
    return operand;
}
