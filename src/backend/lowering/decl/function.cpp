module carven:backend.lowering.decl.function.impl;

import :backend.lowering.program;
import :backend.lowering.decl;
import :backend.lowering.expr;
import :backend.generation.names;
import :backend.lowering.names;
import :backend.lowering.stmt;
import :backend.lowering.types;
import :backend.target.decl;
import :backend.target.expr;
import :backend.target.item;
import :backend.target.stmt;
import :backend.target.symbol;
import :semantic.hir.decl;
import :semantic.hir.type;
import :support.invariant;
import std;

namespace {

auto lower_cpp_import_bridge(
    TargetModuleLowerer& context,
    FunctionID function_id,
    bool declaration_only
) noexcept -> TargetItemValue {
    const auto& function = context.source().function(function_id);
    const auto& callable = context.source().callable(function.callable);
    const auto import_origin = cpp_import_form_origin(callable);
    const auto& signature = context.source().callable_signature(function.callable);
    if (!import_origin.has_value()) {
        invariant_violation("import(cpp) lowering requires an import implementation");
    }
    if (context.source().symbol(function.symbol).module_id != context.active_module_id()
        || signature.parameters.size() != callable.parameters.size()
        || signature.result != callable.result
        || !context.failure_profile(signature.failure_profile).ordered_members.empty()
        || signature.carrier_shape.has_value()) {
        invariant_violation("import(cpp) signature disagrees with its semantic declaration");
    }
    auto parameters = std::vector<TargetParameter>();
    auto argument_names = std::vector<TargetIdentifier>();
    parameters.reserve(signature.parameters.size());
    argument_names.reserve(signature.parameters.size());
    const auto provider_binding = declaration_only
        ? std::optional<TargetIdentifier> {}
        : std::optional {
              context.name_allocator().fresh(TargetTemporaryNameKind::CppProviderPointer)
          };
    for (const auto& parameter : signature.parameters) {
        const auto name = declaration_only
            ? std::optional<TargetIdentifier> {}
            : std::optional {
                  context.name_allocator().fresh(TargetTemporaryNameKind::CppBoundaryParameter)
              };
        if (name.has_value()) {
            argument_names.push_back(*name);
        }
        parameters.push_back({
            .name = name,
            .type = materialize_parameter_type(context, parameter),
        });
    }
    auto body = std::vector<TargetStmtID>();
    if (!declaration_only) {
        auto provider_components = std::vector<TargetIdentifier> {
            TargetIdentifier::from_spelling(context.source().provenance().spelling(function.name)),
        };
        const auto provider_function = context.target().append_expression({
            .value = TargetNameExpr {
                .name = TargetName::globally_qualified(std::move(provider_components)),
            },
        });
        const auto provider_address = call_expression(
            context,
            name_expression(context, TargetSymbol::StdAddressof),
            {provider_function}
        );
        body.push_back(context.target().append_lowering_statement(
            TargetVariableStmt {
                .binding = TargetVariableBinding::ConstValue,
                .name = *provider_binding,
                .type = intrinsic_type(context, TargetSymbol::Auto),
                .initializer = provider_address,
                .maybe_unused = false,
            }
        ));
        auto arguments = std::vector<TargetExprID>();
        arguments.reserve(argument_names.size());
        for (const auto& name : argument_names) {
            arguments.push_back(name_expression(context, TargetName {name}));
        }
        auto call = call_expression(
            context,
            name_expression(context, TargetName {*provider_binding}),
            std::move(arguments)
        );
        if (is_char_type(context, signature.result)) {
            call = call_expression(
                context,
                name_expression(context, TargetSymbol::RuntimeCheckedUnicodeScalar),
                {call}
            );
        }
        body.push_back(context.target().append_lowering_statement(
            is_void_type(context, signature.result)
                ? TargetStmtValue {TargetExprStmt {.expression = call}}
                : TargetStmtValue {TargetReturnStmt {.expression = call}}
        ));
    }
    return TargetDecl {TargetFunctionDecl {
        .name = TargetName {symbol_identifier(context, function.symbol)},
        .parameters = std::move(parameters),
        .result = lower_type(context, signature.result),
        .body = std::move(body),
        .declaration_only = declaration_only,
        .inline_specifier = false,
    }};
}

auto lower_carven_function_declaration(
    TargetModuleLowerer& context,
    FunctionID function_id,
    bool declaration_only
) noexcept -> TargetItemValue {
    const auto& function = context.source().function(function_id);
    const auto& callable = context.source().callable(function.callable);
    const auto body_id = callable_body_id(callable);
    if (!body_id.has_value()) {
        invariant_violation("Carven function lowering requires a semantic body");
    }
    const auto& function_body = context.source().body(*body_id);
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

} // namespace

auto lower_function_declaration(
    TargetModuleLowerer& context,
    FunctionID function_id,
    bool declaration_only
) noexcept -> TargetItemValue {
    const auto& function = context.source().function(function_id);
    const auto& callable = context.source().callable(function.callable);
    return cpp_import_form_origin(callable).has_value()
        ? lower_cpp_import_bridge(context, function_id, declaration_only)
        : lower_carven_function_declaration(context, function_id, declaration_only);
}
