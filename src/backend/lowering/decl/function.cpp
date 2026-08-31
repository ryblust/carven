module carven:backend.lowering.decl.function.impl;

import :backend.lowering.program;
import :backend.lowering.decl;
import :backend.lowering.expr;
import :backend.generation.names;
import :backend.lowering.names;
import :backend.lowering.stmt;
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
    const auto& function = context.source().function(function_id);
    const auto& function_body =
        context.source().body(context.source().callable(function.callable).body);
    const auto& target_signature = context.source().callable_signature(function.callable);
    const auto failure_set = target_signature.failure_profile;
    const auto& failures = context.failure_profile(failure_set).ordered_members;
    const auto result = lower_type(context, target_signature.result);
    const auto target_carrier = target_signature.carrier_shape.has_value()
        ? std::optional {materialize_failure_carrier(context, *target_signature.carrier_shape)}
        : std::nullopt;
    if (failures.empty() != !target_carrier.has_value()) {
        invariant_violation("target callable profile and carrier shape disagree");
    }
    const auto carrier = target_carrier.has_value() ? target_carrier->type : result;
    if (function_body.parameters.size() != target_signature.parameters.size()) {
        invariant_violation("target callable signature does not align with its body parameters");
    }
    auto parameters = std::vector<TargetParameter>();
    auto body = std::vector<TargetStmtID>();
    if (declaration_only) {
        for (const auto& [index, parameter] : std::views::enumerate(function_body.parameters)) {
            parameters.push_back({
                .name = std::nullopt,
                .type = materialize_parameter_type(context, target_signature.parameters[index]),
            });
        }
    } else {
        auto callable_lowerer =
            context.callable(function_body.scope, context.source().block(function_body.root).scope);
        for (const auto& [index, parameter] : std::views::enumerate(function_body.parameters)) {
            const auto* named = std::get_if<HIRNamedBindingTarget>(&parameter.target);
            const auto parameter_name =
                named != nullptr && symbol_is_used(callable_lowerer, named->symbol)
                ? std::optional {symbol_identifier(callable_lowerer, named->symbol)}
                : std::nullopt;
            parameters.push_back({
                .name = parameter_name,
                .type = materialize_parameter_type(
                    callable_lowerer,
                    target_signature.parameters[index]
                ),
            });
        }
        const auto function_control = TargetControlDestinations::callable(
            failures.empty() ? std::nullopt
                             : std::optional {FailureContinuation {
                                   .carrier = *target_carrier,
                                   .local_transfer = std::nullopt,
                               }}
        );
        body = failures.empty()
            ? lower_block(callable_lowerer, function_body.root, function_control)
            : lower_outcome_block(
                  callable_lowerer,
                  function_body.root,
                  target_signature.result,
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
    }};
}
