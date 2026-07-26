module carven:backend.lowering.declarations.function.impl;

import :backend.lowering.program;
import :backend.lowering.declarations;
import :backend.lowering.expressions;
import :backend.generation.names;
import :backend.lowering.names;
import :backend.lowering.statements;
import :backend.lowering.types;
import :backend.target.decl;
import :backend.target.item;
import :backend.target.stmt;
import :semantic.hir.decl;
import :semantic.hir.type;
import :support.invariant;
import std;

auto lower_function_declaration(
    TargetModuleLowerer& context,
    FunctionID function_id,
    bool declaration_only
) noexcept -> TargetItemValue {
    const auto& function = context.semantic().function(function_id);
    const auto& function_body =
        context.semantic().body(context.semantic().callable(function.callable).body);
    const auto failure_set = context.semantic().callable(function.callable).failure_set;
    const auto& failures = context.failure_set(failure_set).ordered_members;
    const auto result = lower_type(context, function.result);
    const auto carrier =
        failures.empty() ? result : failure_carrier_type(context, function.result, failure_set);
    auto parameters = std::vector<TargetParameter>();
    auto body = std::vector<TargetStmtID>();
    if (declaration_only) {
        for (const auto& parameter : function_body.parameters) {
            parameters.push_back({
                .name = std::nullopt,
                .type = parameter_type(
                    context,
                    parameter.access,
                    parameter.type,
                    lower_type(context, parameter.type)
                ),
                .maybe_unused = false,
            });
        }
    } else {
        auto callable_lowerer = context.callable(
            function_body.scope,
            context.semantic().block(function_body.root).scope
        );
        for (const auto& parameter : function_body.parameters) {
            const auto* named = std::get_if<HIRNamedBindingTarget>(&parameter.target);
            const auto parameter_name =
                named != nullptr && symbol_is_used(callable_lowerer, named->symbol)
                ? std::optional {symbol_identifier(callable_lowerer, named->symbol)}
                : std::nullopt;
            parameters.push_back({
                .name = parameter_name,
                .type = parameter_type(
                    callable_lowerer,
                    parameter.access,
                    parameter.type,
                    lower_type(callable_lowerer, parameter.type)
                ),
                .maybe_unused = false,
            });
        }
        const auto function_control = TargetControlDestinations::callable(
            failures.empty()
                ? std::nullopt
                : std::optional {FailureContinuation {
                      .carrier = make_failure_carrier(context, function.result, failure_set),
                      .destination = std::nullopt,
                      .transfer_label = std::nullopt,
                  }}
        );
        body = failures.empty()
            ? lower_block(callable_lowerer, function_body.root, function_control)
            : lower_outcome_block(
                  callable_lowerer,
                  function_body.root,
                  function.result,
                  failure_set,
                  function_control
              );
    }
    return TargetDecl {TargetFunctionDecl {
        .name = TargetName {symbol_identifier(context, function.symbol)},
        .parameters = std::move(parameters),
        .result = carrier,
        .body = std::move(body),
        .declaration_only = declaration_only,
        .inline_specifier = false,
        .constexpr_specifier = false,
    }};
}
