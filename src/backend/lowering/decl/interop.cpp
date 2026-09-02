module carven:backend.lowering.decl.interop.impl;

import :backend.generation.names;
import :backend.generation.program;
import :backend.lowering.decl;
import :backend.lowering.expr;
import :backend.lowering.program;
import :backend.lowering.types;
import :backend.target.decl;
import :backend.target.expr;
import :backend.target.item;
import :backend.target.stmt;
import :backend.target.symbol;
import :semantic.hir.decl;
import :support.invariant;
import std;

namespace {

auto exported_signature(const TargetModuleLowerer& context, FunctionID function_id) noexcept
    -> const TargetCallSignature& {
    const auto& function = context.source().function(function_id);
    const auto& callable = context.source().callable(function.callable);
    const auto& signature = context.source().callable_signature(function.callable);
    if (!function.cpp_export_form_origin.has_value()
        || signature.parameters.size() != callable.parameters.size()
        || signature.result != callable.result
        || !context.failure_profile(signature.failure_profile).ordered_members.empty()
        || signature.carrier_shape.has_value()) {
        invariant_violation("C++ façade lowering requires a sealed export signature");
    }
    return signature;
}

auto public_identifier(const TargetModuleLowerer& context, const HIRFunctionDecl& function) noexcept
    -> TargetIdentifier {
    return TargetIdentifier::from_spelling(context.source().provenance().spelling(function.name));
}

auto internal_function_name(
    const TargetModuleLowerer& context,
    const HIRFunctionDecl& function
) noexcept -> TargetName {
    const auto& module = context.source().module(context.active_module_id());
    auto components = std::vector<TargetIdentifier>(
        module.qualified_namespace_name.components().begin(),
        module.qualified_namespace_name.components().end()
    );
    components.push_back(context.entity_identifier(function.symbol));
    return TargetName::globally_qualified(std::move(components));
}

auto append_export_item(
    TargetModuleLowerer& context,
    const HIRFunctionDecl& function,
    TargetFunctionDecl declaration
) noexcept -> TargetItemID {
    if (!function.cpp_export_form_origin.has_value()) {
        invariant_violation("C++ façade lowering received a non-exported function");
    }
    return context.target().append_item({
        .value = TargetDecl {std::move(declaration)},
        .attribution = {
            .kind = TargetAttributionKind::SourceExpansion,
            .origin = target_source_origin(context, *function.cpp_export_form_origin),
            .reason = std::nullopt,
        },
    });
}

} // namespace

auto lower_cpp_export_header_declaration(
    TargetModuleLowerer& context,
    FunctionID function_id
) noexcept -> TargetItemID {
    const auto& function = context.source().function(function_id);
    const auto& signature = exported_signature(context, function_id);
    auto parameters = std::vector<TargetParameter>();
    parameters.reserve(signature.parameters.size());
    for (const auto& parameter : signature.parameters) {
        parameters.push_back({
            .name = std::nullopt,
            .type = materialize_parameter_type(context, parameter),
        });
    }
    return append_export_item(
        context,
        function,
        TargetFunctionDecl {
            .name = TargetName {public_identifier(context, function)},
            .parameters = std::move(parameters),
            .result = lower_type(context, signature.result),
            .body = {},
            .declaration_only = true,
            .inline_specifier = false,
        }
    );
}

auto lower_cpp_export_facade(TargetModuleLowerer& context, FunctionID function_id) noexcept
    -> TargetItemID {
    const auto& function = context.source().function(function_id);
    const auto& signature = exported_signature(context, function_id);
    auto parameters = std::vector<TargetParameter>();
    auto arguments = std::vector<TargetExprID>();
    parameters.reserve(signature.parameters.size());
    arguments.reserve(signature.parameters.size());
    for (const auto& parameter : signature.parameters) {
        const auto name =
            context.name_allocator().fresh(TargetTemporaryNameKind::CppBoundaryParameter);
        parameters.push_back({
            .name = name,
            .type = materialize_parameter_type(context, parameter),
        });
        auto argument = name_expression(context, TargetName {name});
        if (is_char_type(context, parameter.type)) {
            argument = call_expression(
                context,
                name_expression(context, TargetSymbol::RuntimeCheckedUnicodeScalar),
                {argument}
            );
        }
        arguments.push_back(argument);
    }
    const auto call = call_expression(
        context,
        name_expression(context, internal_function_name(context, function)),
        std::move(arguments)
    );
    const auto statement = context.target().append_lowering_statement(
        is_void_type(context, signature.result)
            ? TargetStmtValue {TargetExprStmt {.expression = call}}
            : TargetStmtValue {TargetReturnStmt {.expression = call}}
    );
    return append_export_item(
        context,
        function,
        TargetFunctionDecl {
            .name = TargetName {public_identifier(context, function)},
            .parameters = std::move(parameters),
            .result = lower_type(context, signature.result),
            .body = {statement},
            .declaration_only = false,
            .inline_specifier = false,
        }
    );
}
