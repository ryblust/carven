module carven:backend.lowering.expr.lower.impl;

import :backend.lowering.program;
import :backend.lowering.expr;
import :backend.generation.names;
import :backend.lowering.names;
import :backend.lowering.match;
import :backend.lowering.stmt;
import :backend.lowering.types;
import :backend.target.expr;
import :backend.target.ids;
import :backend.target.raw;
import :backend.target.stmt;
import :semantic.hir.expr;
import :semantic.hir.ids;
import std;

auto name_expression(TargetModuleLowerer& context, TargetName name) noexcept -> TargetExprID {
    return context.target().append_expression({
        .value = TargetNameExpr {.name = std::move(name)},
    });
}

auto name_expression(TargetModuleLowerer& context, TargetSymbol symbol) noexcept -> TargetExprID {
    return context.target().append_expression({
        .value = TargetIntrinsicNameExpr {.symbol = symbol},
    });
}

auto lower_expression(
    TargetCallableLowerer& context,
    HIRExprID id,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression {
    return consume_failure_carrier(
        context,
        lower_unconsumed_expression(context, id, control),
        control
    );
}

auto lower_unconsumed_expression(
    TargetCallableLowerer& context,
    HIRExprID id,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression {
    const auto& source = context.source().expression(id);
    const auto* cast = std::get_if<HIRCastExpr>(&source.value);
    if (cast != nullptr && cast->kind == HIRCastKind::Identity) {
        return lower_unconsumed_expression(context, cast->operand_id, control);
    }
    return std::visit(
        [&](const auto& value) noexcept -> LoweredExpression {
            using Value = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Value, HIRCppExpr>) {
                return lower_expression(context, id, value);
            } else {
                return lower_expression(context, id, value, control);
            }
        },
        source.value
    );
}

auto lower_tail_expression(
    TargetCallableLowerer& context,
    HIRExprID id,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression {
    const auto& source = context.source().expression(id);
    if (const auto* cast = std::get_if<HIRCastExpr>(&source.value);
        cast != nullptr && cast->kind == HIRCastKind::Identity) {
        return lower_tail_expression(context, cast->operand_id, control);
    }
    if (const auto* propagation = std::get_if<HIRPropagationExpr>(&source.value)) {
        return lower_unconsumed_expression(context, propagation->operand_id, control);
    }
    return lower_unconsumed_expression(context, id, control);
}

auto lower_expression(
    TargetCallableLowerer& context,
    HIRExprID id,
    const HIRCppExpr& expression
) noexcept -> LoweredExpression {
    const auto raw = context.target().append_expression({
        .value = TargetRawFragment {
            .bytes = std::string(context.source().provenance().spelling(expression.bytes)),
        },
    });
    const auto* builtin = std::get_if<HIRBuiltinTypeValue>(
        &context.source().type(context.source().expression(id).type).value
    );
    if (builtin == nullptr
        || (builtin->kind != HIRBuiltinType::Str && builtin->kind != HIRBuiltinType::Char)) {
        return {.prelude = {}, .expression = raw};
    }
    const auto checker = builtin->kind == HIRBuiltinType::Str
        ? TargetSymbol::RuntimeCheckedForeignStr
        : TargetSymbol::RuntimeCheckedForeignChar;
    return {
        .prelude = {},
        .expression = call_expression(context, name_expression(context, checker), {raw}),
    };
}

auto lower_expression(
    TargetCallableLowerer& context,
    HIRExprID id,
    const HIRIfExpr& expression,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression {
    const auto& source = context.source().expression(id);
    const auto& expression_control = context.source().expression_control(id);
    const auto failure_set = carrier_failure_set(context, id);
    const auto effectful = !context.failure_profile(failure_set).ordered_members.empty();
    const auto exits_test = expression_control.exits_test;
    const auto target_result = lower_type(context, source.type);
    const auto carrier = effectful
        ? std::optional {make_failure_carrier(context, source.type, failure_set)}
        : std::nullopt;
    const auto normal_result = carrier.has_value() ? carrier->type : target_result;
    auto branch_control = control.iife();
    if (effectful) {
        branch_control = branch_control.with_failure(
            FailureContinuation {
                .carrier = *carrier,
                .local_transfer = std::nullopt,
            }
        );
    }
    if (exits_test) {
        branch_control = branch_control.with_test_result(normal_result);
    }
    auto alternate = expression.else_branch.has_value() ? effectful
            ? lower_outcome_block(
                  context,
                  *expression.else_branch,
                  source.type,
                  failure_set,
                  branch_control
              )
            : lower_block(context, *expression.else_branch, branch_control, true)
                                                        : std::vector<TargetStmtID>();
    for (auto index = expression.branches.size(); index > 0; --index) {
        const auto& branch = expression.branches[index - 1];
        auto condition = lower_expression(context, branch.condition, branch_control);
        condition.prelude.push_back(context.target().append_lowering_statement(
            TargetIfStmt {
                .branches =
                    {
                        TargetIfBranch {
                            .condition = condition.expression,
                            .body = effectful
                                ? lower_outcome_block(
                                      context,
                                      branch.body,
                                      source.type,
                                      failure_set,
                                      branch_control
                                  )
                                : lower_block(context, branch.body, branch_control, true),
                        },
                    },
                .else_body = alternate.empty() ? std::nullopt
                                               : std::optional<std::vector<TargetStmtID>> {
                                                     std::move(alternate),
                                                 },
            }
        ));
        alternate = std::move(condition.prelude);
    }
    const auto outcome = context.target().append_expression({
        .value = TargetLambdaExpr {
            .reason = TargetIIFEReason::ConditionalExpression,
            .body = std::move(alternate),
        },
    });
    auto normal = exits_test ? propagated_test_value(context, outcome, control)
                             : LoweredExpression {.prelude = {}, .expression = outcome};
    if (!effectful) {
        return normal;
    }
    normal.unconsumed_carrier = *carrier;
    return normal;
}

auto lower_expression(
    TargetCallableLowerer& context,
    HIRExprID id,
    const HIRMatchExpr& expression,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression {
    const auto& source = context.source().expression(id);
    const auto& expression_control = context.source().expression_control(id);
    const auto failure_set = carrier_failure_set(context, id);
    const auto effectful = !context.failure_profile(failure_set).ordered_members.empty();
    const auto exits_test = expression_control.exits_test;
    const auto target_result = lower_type(context, source.type);
    const auto carrier = effectful
        ? std::optional {make_failure_carrier(context, source.type, failure_set)}
        : std::nullopt;
    const auto normal_result = carrier.has_value() ? carrier->type : target_result;
    auto match_control = control.iife();
    if (exits_test) {
        match_control = match_control.with_test_result(normal_result);
    }
    if (!effectful) {
        auto body = lower_value_match(context, expression, match_control);
        const auto match_lambda = context.target().append_expression({
            .value = TargetLambdaExpr {
                .reason = TargetIIFEReason::MatchExpression,
                .body = std::move(body),
            },
        });
        return exits_test ? propagated_test_value(context, match_lambda, control)
                          : LoweredExpression {.prelude = {}, .expression = match_lambda};
    }
    match_control = match_control.with_failure(
        FailureContinuation {
            .carrier = *carrier,
            .local_transfer = std::nullopt,
        }
    );
    auto body = lower_outcome_match(context, expression, match_control, source.type, failure_set);
    const auto outcome = context.target().append_expression({
        .value = TargetLambdaExpr {
            .reason = TargetIIFEReason::MatchExpression,
            .body = std::move(body),
        },
    });
    auto normal = exits_test ? propagated_test_value(context, outcome, control)
                             : LoweredExpression {.prelude = {}, .expression = outcome};
    normal.unconsumed_carrier = *carrier;
    return normal;
}

auto lower_expression(
    TargetCallableLowerer& context,
    HIRExprID id,
    const HIRTryExpr& expression,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression {
    const auto& source = context.source().expression(id);
    const auto& body_source = context.source().block(expression.body);
    const auto& expression_control = context.source().expression_control(id);
    const auto& body_control = context.source().block_control(expression.body);
    const auto& attempt_facts = *context.source().try_facts(id);
    const auto exits_test = expression_control.exits_test;
    if (!body_source.result.has_value() && !is_void_type(context, source.type)) {
        std::unreachable();
    }
    const auto protected_failure_set = body_control.outward_failure_set;
    if (context.failure_profile(protected_failure_set).ordered_members.empty()) {
        auto protected_control = control.iife();
        const auto normal_result = lower_type(context, source.type);
        if (exits_test) {
            protected_control = protected_control.with_test_result(normal_result);
        }
        auto protected_body = lower_block(context, expression.body, protected_control, true);
        const auto protected_lambda = context.target().append_expression({
            .value = TargetLambdaExpr {
                .reason = TargetIIFEReason::TryBody,
                .body = std::move(protected_body),
            },
        });
        return exits_test ? propagated_test_value(context, protected_lambda, control)
                          : LoweredExpression {
                                .prelude = {},
                                .expression = protected_lambda,
                            };
    }
    const auto output_failure_set = carrier_failure_set(context, id);
    const auto effectful = !context.failure_profile(output_failure_set).ordered_members.empty();
    const auto target_result = lower_type(context, source.type);
    const auto carrier = effectful
        ? std::optional {make_failure_carrier(context, source.type, output_failure_set)}
        : std::nullopt;
    const auto normal_result = carrier.has_value() ? carrier->type : target_result;
    auto try_control = control.iife();
    if (effectful) {
        try_control = try_control.with_failure(
            FailureContinuation {
                .carrier = *carrier,
                .local_transfer = std::nullopt,
            }
        );
    }
    if (exits_test) {
        try_control = try_control.with_test_result(normal_result);
    }
    auto protected_control = try_control.iife();
    const auto protected_result_type = body_source.result.has_value()
        ? context.source().expression(*body_source.result).type
        : source.type;
    const auto protected_carrier =
        make_failure_carrier(context, protected_result_type, protected_failure_set);
    if (body_control.exits_test) {
        protected_control = protected_control.with_test_result(protected_carrier.type);
    }
    const auto protected_outcome_control = protected_control.with_failure(
        FailureContinuation {
            .carrier = protected_carrier,
            .local_transfer = std::nullopt,
        }
    );
    auto protected_value = std::optional<LoweredExpression>();
    if (body_source.statements.empty()
        && body_source.result.has_value()
        && !body_control.exits_test) {
        const auto lexical_scope = context.enter_scope(body_source.scope);
        auto direct =
            lower_tail_expression(context, *body_source.result, protected_outcome_control);
        const auto forwards_carrier = direct.unconsumed_carrier.has_value();
        if (forwards_carrier) {
            direct = convert_tail_carrier(context, std::move(direct), protected_carrier);
        } else {
            direct.expression =
                outcome_success_expression(context, protected_carrier.type, direct.expression);
        }
        if (direct.prelude.empty()) {
            protected_value = std::move(direct);
        } else {
            direct.prelude.push_back(context.target().append_lowering_statement(
                TargetReturnStmt {.expression = direct.expression}
            ));
            protected_value = LoweredExpression {
                .prelude = {},
                .expression = context.target().append_expression({
                    .value = TargetLambdaExpr {
                        .reason = TargetIIFEReason::FailureBoundary,
                        .body = std::move(direct.prelude),
                    },
                }),
            };
        }
    }
    if (!protected_value.has_value()) {
        auto protected_body = lower_outcome_block(
            context,
            expression.body,
            protected_result_type,
            protected_failure_set,
            protected_control
        );
        const auto protected_lambda = context.target().append_expression({
            .value = TargetLambdaExpr {
                .reason = TargetIIFEReason::FailureBoundary,
                .body = std::move(protected_body),
            },
        });
        protected_value = body_control.exits_test
            ? propagated_test_value(context, protected_lambda, try_control)
            : LoweredExpression {.prelude = {}, .expression = protected_lambda};
    }
    const auto outcome_name = context.fresh_name(TargetTemporaryNameKind::Try);
    auto body = std::move(protected_value->prelude);
    body.push_back(context.target().append_lowering_statement(
        TargetVariableStmt {
            .binding = TargetVariableBinding::MutableValue,
            .name = outcome_name,
            .type = intrinsic_type(context, TargetSymbol::Auto),
            .initializer = protected_value->expression,
            .maybe_unused = false,
        }
    ));
    const auto has_value = member_call_expression(
        context,
        name_expression(context, TargetName {outcome_name}),
        TargetNameAllocator::fixed("has_value")
    );
    auto success = std::optional<TargetExprID>();
    if (is_void_type(context, source.type)) {
        if (effectful) {
            success = outcome_success_expression(context, carrier->type);
        }
    } else {
        const auto moved_success = call_expression(
            context,
            name_expression(context, TargetSymbol::StdMove),
            {name_expression(context, TargetName {outcome_name})}
        );
        success = member_call_expression(
            context,
            moved_success,
            TargetNameAllocator::fixed("take_value")
        );
        if (effectful) {
            success = outcome_success_expression(context, carrier->type, *success);
        }
    }
    if (exits_test) {
        success = test_success_expression(context, normal_result, success);
    }
    const auto success_return =
        context.target().append_lowering_statement(TargetReturnStmt {.expression = success});
    body.push_back(context.target().append_lowering_statement(
        TargetIfStmt {
            .branches =
                {
                    TargetIfBranch {
                        .condition = has_value,
                        .body = {success_return},
                    },
                },
            .else_body = std::nullopt,
        }
    ));
    auto handlers = lower_catch_handlers(
        context,
        expression.arms,
        attempt_facts.arms,
        protected_carrier,
        outcome_name,
        try_control,
        source.type,
        output_failure_set
    );
    auto unmatched = lower_unhandled_failure_transfer(
        context,
        outcome_name,
        attempt_facts.unhandled_failure_set,
        try_control
    );
    auto completed_handlers =
        complete_catch_handlers(context, std::move(handlers), std::move(unmatched));
    body.insert(
        body.end(),
        std::make_move_iterator(completed_handlers.begin()),
        std::make_move_iterator(completed_handlers.end())
    );
    const auto propagated = context.target().append_expression({
        .value = TargetLambdaExpr {
            .reason = TargetIIFEReason::FailureBoundary,
            .body = std::move(body),
        },
    });
    auto normal = exits_test ? propagated_test_value(context, propagated, control)
                             : LoweredExpression {.prelude = {}, .expression = propagated};
    if (!effectful) {
        return normal;
    }
    normal.unconsumed_carrier = *carrier;
    return normal;
}
