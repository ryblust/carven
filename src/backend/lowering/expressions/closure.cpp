module carven:backend.lowering.expressions.closure.impl;

import :backend.lowering.program;
import :backend.lowering.expressions;
import :backend.generation.names;
import :backend.lowering.names;
import :backend.lowering.statements;
import :backend.lowering.types;
import :backend.target;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.type;
import :semantic.hir;
import :semantic.hir.access;
import :semantic.hir.decl;
import :semantic.hir.expr;
import :semantic.hir.type;
import std;

auto lower_expression(
    TargetCallableLowerer& context,
    HIRExprID,
    const HIRClosureExpr& expression,
    const TargetControlDestinations&
) noexcept -> LoweredExpression {
    const auto& source_body =
        context.semantic().body(context.semantic().callable(expression.callable).body);
    auto body_lowerer = context.nested_callable(
        source_body.scope,
        context.semantic().block(source_body.root).scope
    );
    auto captures = std::vector<TargetClosureCapture>();
    captures.reserve(expression.captures.size());
    for (const auto& capture : expression.captures) {
        captures.push_back({
            .mode = capture.mode == HIRCaptureMode::Value ? TargetCaptureMode::Value
                                                          : TargetCaptureMode::Write,
            .source = symbol_identifier(context, capture.source),
            .name = symbol_identifier(body_lowerer, capture.local),
        });
    }
    auto parameters = std::vector<TargetClosureParameter>();
    parameters.reserve(source_body.parameters.size());
    for (const auto& parameter : source_body.parameters) {
        const auto* named = std::get_if<HIRNamedBindingTarget>(&parameter.target);
        const auto parameter_name = named != nullptr && symbol_is_used(body_lowerer, named->symbol)
            ? std::optional {symbol_identifier(body_lowerer, named->symbol)}
            : std::nullopt;
        parameters.push_back({
            .name = parameter_name,
            .type = parameter_type(
                body_lowerer,
                parameter.access,
                parameter.type,
                lower_type(body_lowerer, parameter.type)
            ),
            .maybe_unused = false,
        });
    }
    const auto failure_set = context.semantic().callable(expression.callable).failure_set;
    const auto& failures = context.failure_set(failure_set).ordered_members;
    const auto result = lower_type(context, expression.result);
    const auto carrier =
        failures.empty() ? result : failure_carrier_type(context, expression.result, failure_set);
    const auto callable_control = TargetControlDestinations::callable(
        failures.empty()
            ? std::nullopt
            : std::optional {FailureContinuation {
                  .carrier = make_failure_carrier(context, expression.result, failure_set),
                  .destination = std::nullopt,
                  .transfer_label = std::nullopt,
              }}
    );
    auto body = failures.empty() ? lower_block(body_lowerer, source_body.root, callable_control)
                                 : lower_outcome_block(
                                       body_lowerer,
                                       source_body.root,
                                       expression.result,
                                       failure_set,
                                       callable_control
                                   );
    return {
        .prelude = {},
        .expression = context.target().append_expression({
            .value = TargetClosureExpr {
                .captures = std::move(captures),
                .parameters = std::move(parameters),
                .result = carrier,
                .body = std::move(body)
            },
        })
    };
}

auto lower_expression(
    TargetCallableLowerer& context,
    HIRExprID id,
    const HIRCallableViewExpr& expression,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression {
    const auto& view = context.semantic().expression(id);
    const auto& source_hir = context.semantic().expression(expression.source);
    const auto* closure =
        std::get_if<HIRClosureTypeValue>(&context.semantic().type(source_hir.type).value);
    auto source = lower_expression(context, expression.source, control);
    if (closure != nullptr
        && closure->capturing
        && std::holds_alternative<HIRClosureExpr>(source_hir.value)) {
        const auto owner_name = context.fresh_name(TargetTemporaryNameKind::Owner);
        source.prelude.push_back(context.target().append_lowering_statement(
            TargetVariableStmt {
                .binding = TargetVariableBinding::ConstValue,
                .name = owner_name,
                .type = intrinsic_type(context, TargetSymbol::Auto),
                .initializer = source.expression,
                .maybe_unused = false
            }
        ));
        source.expression = name_expression(context, TargetName {owner_name});
    }
    source.expression = context.target().append_expression({
        .value = TargetConstructionExpr {
            .type = lower_type(context, view.type),
            .initializer = std::vector<TargetExprID> {source.expression}
        },
    });
    return source;
}

auto lower_expression(
    TargetCallableLowerer& context,
    HIRExprID,
    const HIRPropagationExpr& expression,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression {
    return consume_failure_carrier(
        context,
        lower_unconsumed_expression(context, expression.operand_id, control),
        control
    );
}
