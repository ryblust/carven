module carven:backend.lowering.expressions.call.impl;

import :backend.lowering.program;
import :backend.lowering.expressions;
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
            .template_arguments = {},
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
    TargetExprID operand,
    TargetIdentifier name,
    std::vector<TargetTemplateArgument> template_arguments,
    std::vector<TargetExprID> arguments
) noexcept -> TargetExprID {
    const auto member = context.target().append_expression({
        .value = TargetMemberExpr {
            .operand_id = operand,
            .name = std::move(name),
        },
    });
    return context.target().append_expression({
        .value = TargetCallExpr {
            .callee = member,
            .template_arguments = std::move(template_arguments),
            .arguments = std::move(arguments),
        },
    });
}

namespace {

auto assemble_call(
    TargetCallableLowerer& context,
    LoweredExpression callee,
    std::span<const HIRCallArgument> arguments,
    std::vector<LoweredExpression> lowered_arguments,
    bool foreign_callee,
    std::optional<HIRExprID> callee_source,
    std::optional<TargetMemberName> instance_member = std::nullopt
) noexcept -> LoweredExpression {
    auto materialize_arguments = std::vector<bool>(arguments.size());
    for (auto left = 0uz; left < arguments.size(); ++left) {
        if (is_stable_write_argument(context, arguments[left])) {
            continue;
        }
        for (auto right = left + 1; right < arguments.size(); ++right) {
            if (!lowered_arguments[right].prelude.empty()
                || !TargetEvaluationSequencer::effects_commute(
                    context.semantic()
                        .expression_facts(arguments[left].expression)
                        .evaluation_effect,
                    context.semantic()
                        .expression_facts(arguments[right].expression)
                        .evaluation_effect
                )) {
                materialize_arguments[left] = true;
                break;
            }
        }
    }
    const auto requires_callee_sequencing =
        callee_source.has_value()
        && std::ranges::any_of(
            std::views::iota(0uz, arguments.size()),
            [&](std::size_t index) noexcept {
                return !lowered_arguments[index].prelude.empty()
                    || !TargetEvaluationSequencer::effects_commute(
                        context.semantic().expression_facts(*callee_source).evaluation_effect,
                        context.semantic()
                            .expression_facts(arguments[index].expression)
                            .evaluation_effect
                    );
            }
        );
    if (requires_callee_sequencing
        && (instance_member.has_value() || !is_stable_callee(context, callee.expression))) {
        callee = TargetEvaluationSequencer::materialize(
            context,
            std::move(callee),
            foreign_callee ? MaterializationKind::Preserve
                           : TargetEvaluationSequencer::read_materialization(
                                 context,
                                 context.semantic().expression(*callee_source).type
                             ),
            TargetMaterializationReason::EvaluationOrder
        );
    }
    const auto lowered_callee = instance_member.has_value()
        ? context.target().append_expression({
              .value =
                  TargetMemberExpr {
                      .operand_id = callee.expression,
                      .name = std::move(*instance_member),
                  },
          })
        : callee.expression;
    auto prelude = std::move(callee.prelude);
    auto target_arguments = std::vector<TargetExprID>();
    target_arguments.reserve(arguments.size());
    for (auto index = 0uz; index < arguments.size(); ++index) {
        const auto& argument = arguments[index];
        auto lowered_argument = std::move(lowered_arguments[index]);
        if (materialize_arguments[index]) {
            const auto foreign =
                is_foreign_type(context, context.semantic().expression(argument.expression).type);
            const auto access = foreign ? MaterializationKind::Preserve
                : argument.access == HIRAccessMode::Read
                ? TargetEvaluationSequencer::read_materialization(
                      context,
                      context.semantic().expression(argument.expression).type
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
    const auto call = call_expression(context, lowered_callee, std::move(target_arguments));
    return {.prelude = std::move(prelude), .expression = call};
}

} // namespace

auto ordered_call(
    TargetCallableLowerer& context,
    LoweredExpression callee,
    std::span<const HIRCallArgument> arguments,
    bool foreign_callee,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression {
    return assemble_call(
        context,
        std::move(callee),
        arguments,
        lower_call_arguments(context, arguments, control),
        foreign_callee,
        std::nullopt
    );
}

auto lower_expression(
    TargetCallableLowerer& context,
    HIRExprID id,
    const HIRCallExpr& expression,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression {
    const auto foreign_callee =
        is_foreign_type(context, context.semantic().expression(expression.callee).type);
    const auto* member =
        std::get_if<HIRMemberExpr>(&context.semantic().expression(expression.callee).value);
    const auto* unresolved =
        member == nullptr ? nullptr : std::get_if<HIRUnresolvedMemberTarget>(&member->target);
    const auto delayed_member = foreign_callee && unresolved != nullptr && !unresolved->scope;
    auto callee = delayed_member ? lower_expression(context, member->operand_id, control)
                                 : lower_expression(context, expression.callee, control);
    auto arguments = lower_call_arguments(context, expression.arguments, control);
    auto lowered = assemble_call(
        context,
        std::move(callee),
        expression.arguments,
        std::move(arguments),
        foreign_callee,
        expression.callee,
        delayed_member ? std::optional<TargetMemberName> {member_name(context, *member)}
                       : std::nullopt
    );
    lowered.unconsumed_carrier = call_result_carrier(context, id, expression.callee);
    return lowered;
}
