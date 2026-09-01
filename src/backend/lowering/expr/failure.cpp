module carven:backend.lowering.expr.failure.impl;

import :backend.lowering.program;
import :backend.lowering.expr;
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
            .template_argument_type_ids = {},
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
            .operand_id = member_call_expression(
                context,
                name_expression(context, TargetName {name}),
                TargetNameAllocator::fixed("has_value")
            ),
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
    const auto moved = call_expression(
        context,
        name_expression(context, TargetSymbol::StdMove),
        {name_expression(context, TargetName {name})}
    );
    return {
        .prelude = std::move(prelude),
        .expression = member_call_expression(context, moved, TargetNameAllocator::fixed("value")),
    };
}

auto call_result_carrier(TargetCallableLowerer& context, HIRExprID call, HIRExprID callee) noexcept
    -> std::optional<FailureCarrierDescriptor> {
    const auto result = context.source().expression(call).type;
    const auto& type = context.source().type_recipe(context.source().expression(callee).type).value;
    auto signature_id = std::optional<TargetCallSignatureID>();
    if (const auto* callable = std::get_if<TargetCallableTypeRecipe>(&type)) {
        signature_id = callable->signature;
    } else if (const auto* function = std::get_if<TargetFunctionReferenceTypeRecipe>(&type)) {
        signature_id = function->signature;
    }
    if (!signature_id.has_value()) {
        return std::nullopt;
    }
    const auto& signature = context.source().call_signature(*signature_id);
    if (!signature.carrier_shape.has_value()) {
        return std::nullopt;
    }
    const auto& shape = context.source().carrier_shape(*signature.carrier_shape);
    if (shape.result != result) {
        invariant_violation("target call result disagrees with its sealed carrier shape");
    }
    return materialize_failure_carrier(context, *signature.carrier_shape);
}
namespace {

auto failure_transfer_statements(
    TargetCallableLowerer& context,
    TargetExprID failure_value,
    const TargetControlDestinations& control
) noexcept -> std::vector<TargetStmtID> {
    const auto& continuation = control.failure;
    if (!continuation.has_value()) {
        invariant_violation("failure transfer requires a continuation");
    }
    if (!continuation->local_transfer.has_value()
        && control.test_exit.has_value()
        && control.test_exit->result.has_value()) {
        failure_value = test_success_expression(context, *control.test_exit->result, failure_value);
    }
    if (!continuation->local_transfer.has_value()) {
        return {context.target().append_lowering_statement(
            TargetReturnStmt {.expression = failure_value}
        )};
    }
    const auto& local_transfer = *continuation->local_transfer;
    return {
        context.target().append_lowering_statement(
            TargetAssignmentStmt {
                .target = name_expression(context, TargetName {local_transfer.destination_name}),
                .op = TargetAssignmentOperator::Assign,
                .value = failure_value,
            }
        ),
        context.target().append_statement({
            .value =
                TargetGotoStmt {
                    .label = local_transfer.transfer_label,
                    .role = TargetJumpRole::FailureTransfer,
                },
            .attribution = {
                .kind = TargetAttributionKind::SourceExpansion,
                .origin = std::nullopt,
                .reason = TargetSyntheticReason::FailureTransport,
            },
        }),
    };
}

auto apply_carrier_conversion(
    TargetCallableLowerer& context,
    TargetExprID expression,
    const FailureCarrierDescriptor& destination,
    CarrierInputCategory input_category,
    TargetCarrierConversion conversion
) noexcept -> TargetExprID {
    const auto input = input_category == CarrierInputCategory::Lvalue
        ? call_expression(context, name_expression(context, TargetSymbol::StdMove), {expression})
        : expression;
    switch (conversion) {
        case TargetCarrierConversion::Identity: return input;
        case TargetCarrierConversion::Widen:    {
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
            .operand_id = member_call_expression(
                context,
                name_expression(context, TargetName {operand_name}),
                TargetNameAllocator::fixed("has_value")
            ),
        },
    });
    const auto& failure = control.failure;
    auto failure_body = std::vector<TargetStmtID>();
    if (!failure.has_value()) {
        const auto abort =
            call_expression(context, name_expression(context, TargetSymbol::StdAbort), {});
        failure_body.push_back(
            context.target().append_lowering_statement(TargetExprStmt {.expression = abort})
        );
    } else if (context.source().carrier_shape(source_carrier.shape).result
               == context.source().carrier_shape(failure->carrier.shape).result) {
        const auto conversion =
            classify_carrier_conversion(context, source_carrier, failure->carrier);
        switch (conversion) {
            case TargetCarrierConversion::Identity: {
                const auto direct_return = !failure->local_transfer.has_value()
                    && !(control.test_exit.has_value() && control.test_exit->result.has_value());
                const auto forwarded = direct_return
                    ? name_expression(context, TargetName {operand_name})
                    : call_expression(
                          context,
                          name_expression(context, TargetSymbol::StdMove),
                          {name_expression(context, TargetName {operand_name})}
                      );
                failure_body = failure_transfer_statements(context, forwarded, control);
                break;
            }
            case TargetCarrierConversion::Widen: {
                const auto widened = apply_carrier_conversion(
                    context,
                    name_expression(context, TargetName {operand_name}),
                    failure->carrier,
                    CarrierInputCategory::Lvalue,
                    conversion
                );
                failure_body = failure_transfer_statements(context, widened, control);
                break;
            }
        }
    } else {
        const auto& source_shape = context.source().carrier_shape(source_carrier.shape);
        const auto& destination_shape = context.source().carrier_shape(failure->carrier.shape);
        static_cast<void>(context.source().classify_failure_profile_conversion(
            source_shape.failure_profile,
            destination_shape.failure_profile
        ));
        failure_body = lower_unhandled_failure_transfer(
            context,
            operand_name,
            source_shape.failure_profile,
            control
        );
    }
    prelude.push_back(context.target().append_lowering_statement(
        TargetIfStmt {
            .branches = {TargetIfBranch {.condition = failed, .body = std::move(failure_body)}},
            .else_body = std::nullopt,
        }
    ));
    const auto moved = call_expression(
        context,
        name_expression(context, TargetSymbol::StdMove),
        {name_expression(context, TargetName {operand_name})}
    );
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
    const auto conversion = classify_carrier_conversion(context, source, destination);
    return apply_carrier_conversion(context, expression, destination, input_category, conversion);
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
    const TargetIdentifier& outcome_name,
    FailureSetID failure_set,
    const TargetControlDestinations& control
) noexcept -> std::vector<TargetStmtID> {
    auto statements = std::vector<TargetStmtID>();
    const auto& continuation = control.failure;
    if (continuation.has_value()) {
        for (const auto failure : context.failure_profile(failure_set).ordered_members) {
            const auto target_failure = lower_type(context, failure);
            const auto condition = member_call_expression(
                context,
                name_expression(context, TargetName {outcome_name}),
                TargetNameAllocator::fixed("holds_failure"),
                {target_failure}
            );
            const auto moved = call_expression(
                context,
                name_expression(context, TargetSymbol::StdMove),
                {name_expression(context, TargetName {outcome_name})}
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
    return context.source().expression_control(expression).evaluation_failure_set;
}
