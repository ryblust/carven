module carven:backend.lowering.expressions.failure.impl;

import :backend.lowering.program;
import :backend.lowering.expressions;
import :backend.generation.names;
import :backend.lowering.names;
import :backend.lowering.types;
import :backend.target;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.type;
import :semantic.hir;
import :semantic.hir.expr;
import :semantic.hir.type;
import :support.invariant;
import std;

auto outcome_success_expression(
    TargetCallableLowerer& context,
    TargetTypeID carrier,
    std::optional<TargetExprID> value
) noexcept -> TargetExprID {
    return call_expression(
        context,
        static_member_expression(context, carrier, TargetNameAllocator::fixed("success")),
        value.has_value() ? std::vector<TargetExprID> {*value} : std::vector<TargetExprID>()
    );
}

auto outcome_failure_expression(
    TargetCallableLowerer& context,
    TargetTypeID carrier,
    TargetExprID value
) noexcept -> TargetExprID {
    const auto callee =
        static_member_expression(context, carrier, TargetNameAllocator::fixed("failure"));
    return context.target().append_expression({
        .value = TargetCallExpr {
            .callee = callee,
            .template_arguments = {},
            .arguments = {value},
        },
    });
}

auto take_outcome_failure(
    TargetCallableLowerer& context,
    TargetExprID outcome,
    TargetTypeID failure
) noexcept -> TargetExprID {
    const auto moved =
        call_expression(context, name_expression(context, TargetSymbol::StdMove), {outcome});
    return member_call_expression(
        context,
        moved,
        TargetNameAllocator::fixed("take_failure"),
        {failure}
    );
}

auto test_success_expression(
    TargetCallableLowerer& context,
    TargetTypeID result,
    std::optional<TargetExprID> value
) noexcept -> TargetExprID {
    return call_expression(
        context,
        static_member_expression(
            context,
            test_control_type(context, result),
            TargetNameAllocator::fixed("success")
        ),
        value.has_value() ? std::vector<TargetExprID> {*value} : std::vector<TargetExprID>()
    );
}

auto test_exit_statement(
    TargetCallableLowerer& context,
    const TargetControlDestinations& control
) noexcept -> TargetStmtID {
    const auto& continuation = control.test_exit;
    if (!continuation.has_value()) {
        invariant_violation("test exit has no lowering continuation");
    }
    auto result = std::optional<TargetExprID>();
    if (continuation->result.has_value()) {
        result = call_expression(
            context,
            static_member_expression(
                context,
                test_control_type(context, *continuation->result),
                TargetNameAllocator::fixed("exit")
            ),
            {}
        );
    }
    return context.target().append_lowering_statement(TargetReturnStmt {.expression = result});
}

auto propagated_test_value(
    TargetCallableLowerer& context,
    TargetExprID value,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression {
    const auto name = context.fresh_name(TargetTemporaryNameKind::Outcome);
    const auto temporary = name_expression(context, TargetName {name});
    auto prelude = std::vector<TargetStmtID> {
        context.target().append_lowering_statement(
            TargetVariableStmt {
                .binding = TargetVariableBinding::MutableValue,
                .name = name,
                .type = intrinsic_type(context, TargetSymbol::Auto),
                .initializer = value,
                .maybe_unused = false,
            }
        ),
    };
    const auto exited = context.target().append_expression({
        .value = TargetPrefixExpr {
            .op = TargetPrefixOperator::LogicalNot,
            .operand_id =
                member_call_expression(context, temporary, TargetNameAllocator::fixed("has_value")),
        },
    });
    prelude.push_back(context.target().append_lowering_statement(
        TargetIfStmt {
            .branches =
                {
                    TargetIfBranch {
                        .condition = exited,
                        .body = {test_exit_statement(context, control)},
                    },
                },
            .else_body = std::nullopt,
        }
    ));
    const auto moved =
        call_expression(context, name_expression(context, TargetSymbol::StdMove), {temporary});
    return {
        .prelude = std::move(prelude),
        .expression = member_call_expression(context, moved, TargetNameAllocator::fixed("value")),
    };
}

auto call_result_carrier(TargetCallableLowerer& context, HIRExprID call, HIRExprID callee) noexcept
    -> std::optional<FailureCarrierDescriptor> {
    const auto result = context.semantic().expression(call).type;
    const auto& type = context.semantic().type(context.semantic().expression(callee).type).value;
    if (const auto* function = std::get_if<HIRFunctionTypeValue>(&type)) {
        const auto failure_set = context.semantic().callable(function->callable).failure_set;
        if (context.failure_set(failure_set).ordered_members.empty()) {
            return std::nullopt;
        }
        return make_failure_carrier(context, result, failure_set);
    }
    if (const auto* function = std::get_if<HIRFunctionRefTypeValue>(&type)) {
        const auto& signature = context.semantic().callable_signature(function->signature);
        return context.failure_set(signature.failure_set).ordered_members.empty()
            ? std::nullopt
            : std::optional {make_failure_carrier(context, result, signature.failure_set)};
    }
    if (const auto* closure = std::get_if<HIRClosureTypeValue>(&type)) {
        const auto failure_set = context.semantic().callable(closure->callable).failure_set;
        return !context.failure_set(failure_set).ordered_members.empty()
            ? std::optional {make_failure_carrier(context, result, failure_set)}
            : std::nullopt;
    }
    return std::nullopt;
}

namespace {

auto failure_transfer_statements(
    TargetCallableLowerer& context,
    TargetExprID failure_value,
    const TargetControlDestinations& control
) noexcept -> std::vector<TargetStmtID> {
    const auto& continuation = control.failure;
    if (!continuation.has_value()) {
        const auto abort =
            call_expression(context, name_expression(context, TargetSymbol::StdAbort), {});
        return {context.target().append_lowering_statement(TargetExprStmt {.expression = abort})};
    }
    if (!continuation->transfer_label.has_value()
        && control.test_exit.has_value()
        && control.test_exit->result.has_value()) {
        failure_value = test_success_expression(context, *control.test_exit->result, failure_value);
    }
    if (!continuation->transfer_label.has_value()) {
        return {context.target().append_lowering_statement(
            TargetReturnStmt {.expression = failure_value}
        )};
    }
    if (!continuation->destination.has_value()) {
        invariant_violation("local failure transfer has no destination");
    }
    return {
        context.target().append_lowering_statement(
            TargetAssignmentStmt {
                .target = *continuation->destination,
                .op = TargetAssignmentOperator::Assign,
                .value = failure_value,
            }
        ),
        context.target().append_statement({
            .value =
                TargetGotoStmt {
                    .label = *continuation->transfer_label,
                    .kind = TargetSyntheticControlKind::StatementTryFailureForward,
                },
            .attribution = {
                .kind = TargetAttributionKind::SourceExpansion,
                .origin = std::nullopt,
                .reason = TargetSyntheticReason::FailureTransport,
            },
        }),
    };
}

} // namespace

auto consume_failure_carrier(
    TargetCallableLowerer& context,
    LoweredExpression outcome_value,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression {
    if (!outcome_value.unconsumed_carrier.has_value()) {
        return outcome_value;
    }
    const auto source_carrier = *outcome_value.unconsumed_carrier;
    const auto operand_name = context.fresh_name(TargetTemporaryNameKind::Outcome);
    const auto operand = name_expression(context, TargetName {operand_name});
    auto prelude = std::move(outcome_value.prelude);
    prelude.push_back(context.target().append_lowering_statement(
        TargetVariableStmt {
            .binding = TargetVariableBinding::MutableValue,
            .name = operand_name,
            .type = intrinsic_type(context, TargetSymbol::Auto),
            .initializer = outcome_value.expression,
            .maybe_unused = false,
        }
    ));
    const auto failed = context.target().append_expression({
        .value = TargetPrefixExpr {
            .op = TargetPrefixOperator::LogicalNot,
            .operand_id =
                member_call_expression(context, operand, TargetNameAllocator::fixed("has_value")),
        },
    });
    const auto& failure = control.failure;
    auto failure_body = std::vector<TargetStmtID>();
    if (!failure.has_value()) {
        failure_body = failure_transfer_statements(context, operand, control);
    } else if (source_carrier.result == failure->carrier.result) {
        switch (classify_carrier_conversion(context, source_carrier, failure->carrier)) {
            case CarrierConversion::Identity: {
                const auto direct_return = !failure->transfer_label.has_value()
                    && !(control.test_exit.has_value() && control.test_exit->result.has_value());
                const auto forwarded = direct_return
                    ? operand
                    : call_expression(
                          context,
                          name_expression(context, TargetSymbol::StdMove),
                          {operand}
                      );
                failure_body = failure_transfer_statements(context, forwarded, control);
                break;
            }
            case CarrierConversion::Widen: {
                const auto widened = convert_carrier_expression(
                    context,
                    operand,
                    source_carrier,
                    failure->carrier,
                    CarrierInputCategory::Lvalue
                );
                failure_body = failure_transfer_statements(context, widened, control);
                break;
            }
        }
    } else {
        const auto& source_members =
            context.semantic().failure_set(source_carrier.failure_set).members;
        const auto& destination_members =
            context.semantic().failure_set(failure->carrier.failure_set).members;
        const auto accepted = std::ranges::all_of(source_members, [&](HIRTypeID source) noexcept {
            return std::ranges::contains(destination_members, source);
        });
        if (!accepted) {
            invariant_violation("unconsumed failure carrier is not covered by its continuation");
        }
        failure_body =
            lower_unhandled_failure_transfer(context, operand, source_carrier.failure_set, control);
    }
    prelude.push_back(context.target().append_lowering_statement(
        TargetIfStmt {
            .branches = {TargetIfBranch {.condition = failed, .body = std::move(failure_body)}},
            .else_body = std::nullopt,
        }
    ));
    const auto moved =
        call_expression(context, name_expression(context, TargetSymbol::StdMove), {operand});
    return {
        .prelude = std::move(prelude),
        .expression =
            member_call_expression(context, moved, TargetNameAllocator::fixed("take_value")),
        .unconsumed_carrier = std::nullopt,
    };
}

auto convert_carrier_expression(
    TargetCallableLowerer& context,
    TargetExprID expression,
    const FailureCarrierDescriptor& source,
    const FailureCarrierDescriptor& destination,
    CarrierInputCategory input_category
) noexcept -> TargetExprID {
    const auto input = input_category == CarrierInputCategory::Lvalue
        ? call_expression(context, name_expression(context, TargetSymbol::StdMove), {expression})
        : expression;
    switch (classify_carrier_conversion(context, source, destination)) {
        case CarrierConversion::Identity: return input;
        case CarrierConversion::Widen:    {
            return context.target().append_expression({
                .value = TargetConstructionExpr {
                    .type = destination.type,
                    .initializer = std::vector<TargetExprID> {input},
                },
            });
        }
    }
    std::unreachable();
}

auto convert_tail_carrier(
    TargetCallableLowerer& context,
    LoweredExpression expression,
    const FailureCarrierDescriptor& destination
) noexcept -> LoweredExpression {
    if (!expression.unconsumed_carrier.has_value()) {
        invariant_violation("tail carrier conversion requires an unconsumed carrier");
    }
    const auto& source = *expression.unconsumed_carrier;
    expression.expression = convert_carrier_expression(
        context,
        expression.expression,
        source,
        destination,
        CarrierInputCategory::Rvalue
    );
    expression.unconsumed_carrier.reset();
    return expression;
}

auto lower_unhandled_failure_transfer(
    TargetCallableLowerer& context,
    TargetExprID outcome,
    FailureSetID failure_set,
    const TargetControlDestinations& control
) noexcept -> std::vector<TargetStmtID> {
    auto statements = std::vector<TargetStmtID>();
    const auto& continuation = control.failure;
    if (continuation.has_value()) {
        for (const auto failure : context.failure_set(failure_set).ordered_members) {
            const auto target_failure = lower_type(context, failure);
            const auto condition = member_call_expression(
                context,
                outcome,
                TargetNameAllocator::fixed("holds_failure"),
                {target_failure}
            );
            const auto moved = call_expression(
                context,
                name_expression(context, TargetSymbol::StdMove),
                {outcome}
            );
            const auto extracted = member_call_expression(
                context,
                moved,
                TargetNameAllocator::fixed("take_failure"),
                {target_failure}
            );
            const auto carrier =
                outcome_failure_expression(context, continuation->carrier.type, extracted);
            statements.push_back(context.target().append_lowering_statement(
                TargetIfStmt {
                    .branches = {TargetIfBranch {
                        .condition = condition,
                        .body = failure_transfer_statements(context, carrier, control),
                    }},
                    .else_body = std::nullopt,
                }
            ));
        }
    }
    const auto abort =
        call_expression(context, name_expression(context, TargetSymbol::StdAbort), {});
    statements.push_back(
        context.target().append_lowering_statement(TargetExprStmt {.expression = abort})
    );
    return statements;
}

auto carrier_failure_set(const TargetCallableLowerer& context, HIRExprID expression) noexcept
    -> FailureSetID {
    return context.semantic().expression_facts(expression).evaluation_failure_set;
}
