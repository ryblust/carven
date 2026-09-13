module carven:backend.lowering.decl.function.impl;

import :backend.generation.names;
import :backend.generation.plan;
import :backend.lowering.body;
import :backend.realization.body;
import :backend.lowering.context;
import :backend.lowering.decl;
import :backend.lowering.decl.lowerer;
import :backend.target.builder;
import :backend.target.decl;
import :backend.target.expr;
import :backend.target.item;
import :backend.target.raw;
import :backend.target.stmt;
import :backend.target.symbol;
import :backend.target.type;
import :backend.target.unit;
import :semantic.semir;
import :support.invariant;
import :support.visit;
import std;

auto is_char_type(const SemIRProgram& semantic, TypeID type) noexcept -> bool {
    const auto* builtin = std::get_if<BuiltinTypeValue>(&semantic.types().type(type).value);
    return builtin != nullptr && builtin->kind == BuiltinType::Char;
}

namespace {

constexpr auto callable_scope = TargetScopeID {.ordinal = 0};

auto parameter_identifier(
    const ModuleLowering& context,
    TargetNameAllocator& names,
    const SemIRBody& body,
    LocalBindingID binding
) noexcept -> TargetIdentifier {
    const auto& row = body.binding(binding);
    return names.local_symbol(
        context.semantic().provenance().spelling(row.name),
        binding.index(),
        callable_scope
    );
}

auto lower_cpp_import(
    ModuleLowering& context,
    FunctionID function_id,
    bool declaration_only
) noexcept -> TargetDecl {
    const auto& declarations = context.semantic().declarations();
    const auto& function = declarations.function(function_id);
    const auto& callable = declarations.callable(function.callable);
    if (!cpp_import_form_origin(callable).has_value()) {
        invariant_violation("C++ import lowering received a Carven callable");
    }
    const auto& semantic_signature =
        context.semantic().callable_signatures().signature(callable.signature);
    if (!context.plan().failure_abi().members(semantic_signature.failures).empty()) {
        invariant_violation("C++ import lowering received a fallible signature");
    }
    auto names = context.make_callable_name_allocator();
    auto parameters = std::vector<TargetParameter>();
    auto argument_names = std::vector<TargetIdentifier>();
    for (auto index = 0uz; index < semantic_signature.parameters.size(); ++index) {
        auto name = declaration_only
            ? std::optional<TargetIdentifier>()
            : std::optional<TargetIdentifier> {
                  names.fresh(TargetTemporaryNameKind::CppBoundaryParameter)
              };
        if (name.has_value()) {
            argument_names.push_back(*name);
        }
        parameters.push_back({
            .name = std::move(name),
            .type = context.lower_parameter(semantic_signature.parameters[index]),
        });
    }
    auto body = std::vector<TargetStmt>();
    if (!declaration_only) {
        const auto provider = names.fresh(TargetTemporaryNameKind::CppProviderPointer);
        auto provider_name = std::vector<TargetIdentifier>();
        provider_name.push_back(
            TargetIdentifier::from_spelling(context.semantic().provenance().spelling(function.name))
        );
        body.push_back(generated_statement(
            TargetVariableStmt {
                .binding = TargetVariableBinding::ConstValue,
                .maybe_unused = false,
                .name = provider,
                .type = context.intrinsic_type(TargetSymbol::Auto),
                .initializer = call_expression(
                    intrinsic_expression(TargetSymbol::StdAddressof),
                    target_expressions(
                        name_expression(TargetName::globally_qualified(std::move(provider_name)))
                    )
                ),
            }
        ));
        auto arguments = std::vector<TargetExpr>();
        for (const auto& name : argument_names) {
            arguments.push_back(name_expression(name));
        }
        auto call = call_expression(name_expression(provider), std::move(arguments));
        if (is_char_type(context.semantic(), semantic_signature.result)) {
            call = call_expression(
                intrinsic_expression(TargetSymbol::RuntimeCheckedUnicodeScalar),
                target_expressions(std::move(call))
            );
        }
        if (context.is_void(semantic_signature.result)) {
            body.push_back(generated_statement(
                TargetExprStmt {
                    .expression = std::move(call),
                }
            ));
        } else {
            body.push_back(generated_statement(
                TargetReturnStmt {
                    .expression = std::move(call),
                }
            ));
        }
    }
    return TargetFunctionDecl {
        .name = TargetName {context.names().function_identifier(function_id)},
        .parameters = std::move(parameters),
        .result = context.callable_result(function.callable),
        .form = declaration_only ? TargetFreeFunctionForm {TargetFreeFunctionDeclaration {}}
                                 : TargetFreeFunctionForm {TargetFreeFunctionDefinition {
                                       .body = std::move(body),
                                   }},
        .static_specifier = false,
        .inline_specifier = false,
    };
}

auto lower_carven_function(
    ModuleLowering& context,
    FunctionID function_id,
    bool declaration_only
) noexcept -> TargetDecl {
    const auto& declarations = context.semantic().declarations();
    const auto& function = declarations.function(function_id);
    const auto& callable = declarations.callable(function.callable);
    const auto body_id = callable_body_id(callable);
    if (!body_id.has_value()) {
        invariant_violation("Carven function lowering requires a body implementation");
    }
    const auto& body = context.semantic().bodies().body(*body_id);
    const auto& signature = context.semantic().callable_signatures().signature(callable.signature);
    if (body.inputs().parameters.size() != signature.parameters.size()
        || !body.inputs().captures.empty()) {
        invariant_violation("function body inputs do not match its callable signature");
    }
    auto parameters = std::vector<TargetParameter>();
    auto inputs = BodyRealizationInputs {
        .parameters = {},
        .captures = {},
        .exit = CallableBodyExit {.callable_id = function.callable},
    };
    auto names = context.make_callable_name_allocator();
    for (auto index = 0uz; index < signature.parameters.size(); ++index) {
        auto name = declaration_only
            ? std::optional<TargetIdentifier>()
            : std::optional<TargetIdentifier> {
                  parameter_identifier(context, names, body, body.inputs().parameters[index])
              };
        if (name.has_value()) {
            inputs.parameters.push_back(*name);
        }
        parameters.push_back({
            .name = std::move(name),
            .type = context.lower_parameter(signature.parameters[index]),
        });
    }
    auto statements = std::vector<TargetStmt>();
    if (!declaration_only) {
        auto lowered = lower_body(context, *body_id, std::move(inputs));
        if (lowered.referenced_parameters.size() != parameters.size()) {
            invariant_violation("lowered function parameter reference facts are incomplete");
        }
        for (auto index = 0uz; index < parameters.size(); ++index) {
            if (!lowered.referenced_parameters[index]) {
                parameters[index].name.reset();
            }
        }
        statements = std::move(lowered.statements);
    }
    return TargetFunctionDecl {
        .name = TargetName {context.names().function_identifier(function_id)},
        .parameters = std::move(parameters),
        .result = context.callable_result(function.callable),
        .form = declaration_only ? TargetFreeFunctionForm {TargetFreeFunctionDeclaration {}}
                                 : TargetFreeFunctionForm {TargetFreeFunctionDefinition {
                                       .body = std::move(statements),
                                   }},
        .static_specifier = false,
        .inline_specifier = false,
    };
}

} // namespace

auto lower_function(ModuleLowering& context, FunctionID function, bool declaration_only) noexcept
    -> TargetDecl {
    const auto& callable = context.semantic().declarations().callable(
        context.semantic().declarations().function(function).callable
    );
    return cpp_import_form_origin(callable).has_value()
        ? lower_cpp_import(context, function, declaration_only)
        : lower_carven_function(context, function, declaration_only);
}
