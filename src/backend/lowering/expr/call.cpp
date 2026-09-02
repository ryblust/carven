module carven:backend.lowering.expr.call.impl;

import :backend.lowering.program;
import :backend.lowering.expr;
import :backend.generation.names;
import :backend.lowering.names;
import :backend.lowering.types;
import :backend.target;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.type;
import :semantic.hir.access;
import :semantic.hir.expr;
import :semantic.hir.type;
import std;

namespace {

auto is_stable_callee(TargetCallableLowerer& context, TargetExprID expression) noexcept -> bool {
    const auto& value = context.target().expression(expression).value;
    return std::holds_alternative<TargetIntrinsicNameExpr>(value)
        || std::holds_alternative<TargetNameExpr>(value)
        || std::holds_alternative<TargetScopeMemberExpr>(value);
}

auto is_stable_write_argument(
    const TargetCallableLowerer& context,
    const HIRCallArgument& argument
) noexcept -> bool {
    return argument.access == HIRAccessMode::Write
        && TargetEvaluationSequencer::stable_place_source(context, argument.expression);
}

auto lower_call_arguments(
    TargetCallableLowerer& context,
    std::span<const HIRCallArgument> arguments,
    const TargetControlDestinations& control
) noexcept -> std::vector<LoweredExpression> {
    auto result = std::vector<LoweredExpression>();
    result.reserve(arguments.size());
    for (const auto& argument : arguments) {
        result.push_back(
            argument.access == HIRAccessMode::Write
                ? lower_mutation_expression(context, argument.expression, control)
                : lower_expression(context, argument.expression, control)
        );
    }
    return result;
}

} // namespace

auto call_expression(
    TargetModuleLowerer& context,
    TargetExprID callee,
    std::vector<TargetExprID> arguments
) noexcept -> TargetExprID {
    return context.target().append_expression({
        .value = TargetCallExpr {
            .callee = callee,
            .template_argument_type_ids = {},
            .arguments = std::move(arguments),
        },
    });
}

auto static_member_expression(
    TargetModuleLowerer& context,
    TargetTypeID owner,
    TargetIdentifier name
) noexcept -> TargetExprID {
    return context.target().append_expression({
        .value = TargetStaticMemberExpr {
            .owner = owner,
            .name = std::move(name),
        },
    });
}

auto member_call_expression(
    TargetModuleLowerer& context,
    TargetExprID operand_id,
    TargetIdentifier name,
    std::vector<TargetTypeID> template_argument_type_ids,
    std::vector<TargetExprID> argument_ids
) noexcept -> TargetExprID {
    const auto member_id = context.target().append_expression({
        .value = TargetMemberExpr {
            .operand_id = operand_id,
            .name = std::move(name),
        },
    });
    return context.target().append_expression({
        .value = TargetCallExpr {
            .callee = member_id,
            .template_argument_type_ids = std::move(template_argument_type_ids),
            .arguments = std::move(argument_ids),
        },
    });
}

namespace {

auto assemble_call(
    TargetCallableLowerer& context,
    LoweredExpression callee,
    std::span<const HIRCallArgument> arguments,
    std::vector<LoweredExpression> lowered_arguments,
    std::optional<HIRExprID> callee_source
) noexcept -> LoweredExpression {
    auto materialize_arguments = std::vector<bool>(arguments.size());
    for (auto left = 0uz; left < arguments.size(); ++left) {
        if (is_stable_write_argument(context, arguments[left])) {
            continue;
        }
        for (auto right = left + 1; right < arguments.size(); ++right) {
            if (!lowered_arguments[right].prelude.empty()
                || !TargetEvaluationSequencer::expressions_commute(
                    context,
                    arguments[left].expression,
                    arguments[right].expression
                )) {
                materialize_arguments[left] = true;
                break;
            }
        }
    }
    const auto requires_callee_sequencing = callee_source.has_value()
        && std::ranges::any_of(std::views::iota(0uz, arguments.size()),
                               [&](std::size_t index) noexcept {
                                   return !lowered_arguments[index].prelude.empty()
                                       || !TargetEvaluationSequencer::expressions_commute(
                                           context,
                                           *callee_source,
                                           arguments[index].expression
                                       );
                               });
    if (requires_callee_sequencing && !is_stable_callee(context, callee.expression)) {
        callee = TargetEvaluationSequencer::materialize(
            context,
            std::move(callee),
            TargetEvaluationSequencer::read_materialization(
                context,
                context.source().expression(*callee_source).type
            ),
            TargetMaterializationReason::EvaluationOrder
        );
    }
    auto prelude = std::move(callee.prelude);
    auto target_arguments = std::vector<TargetExprID>();
    target_arguments.reserve(arguments.size());
    for (auto index = 0uz; index < arguments.size(); ++index) {
        const auto& argument = arguments[index];
        auto lowered_argument = std::move(lowered_arguments[index]);
        if (materialize_arguments[index]) {
            const auto access = argument.access == HIRAccessMode::Read
                ? TargetEvaluationSequencer::read_materialization(
                      context,
                      context.source().expression(argument.expression).type
                  )
                : argument.access == HIRAccessMode::Write ? MaterializationKind::WriteReference
                                                          : MaterializationKind::Take;
            lowered_argument = TargetEvaluationSequencer::materialize(
                context,
                std::move(lowered_argument),
                access,
                argument.access == HIRAccessMode::Take
                    ? TargetMaterializationReason::Ownership
                    : TargetMaterializationReason::EvaluationOrder
            );
        }
        prelude.insert(
            prelude.end(),
            std::make_move_iterator(lowered_argument.prelude.begin()),
            std::make_move_iterator(lowered_argument.prelude.end())
        );
        target_arguments.push_back(lowered_argument.expression);
    }
    const auto call = call_expression(context, callee.expression, std::move(target_arguments));
    return {.prelude = std::move(prelude), .expression = call};
}

} // namespace

auto ordered_call(
    TargetCallableLowerer& context,
    LoweredExpression callee,
    std::span<const HIRCallArgument> arguments,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression {
    return assemble_call(
        context,
        std::move(callee),
        arguments,
        lower_call_arguments(context, arguments, control),
        std::nullopt
    );
}

auto lower_expression(
    TargetCallableLowerer& context,
    HIRExprID id,
    const HIRCallExpr& expression,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression {
    auto callee = lower_expression(context, expression.callee, control);
    auto arguments = lower_call_arguments(context, expression.arguments, control);
    auto lowered = assemble_call(
        context,
        std::move(callee),
        expression.arguments,
        std::move(arguments),
        expression.callee
    );
    lowered.unconsumed_carrier = call_result_carrier(context, id, expression.callee);
    return lowered;
}
