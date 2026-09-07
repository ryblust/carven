module carven:backend.lowering.decl.impl;

import :backend.generation.names;
import :backend.generation.plan;
import :backend.lowering.body;
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

namespace {

auto append_items(std::vector<TargetItem>& destination, std::vector<TargetItem> source) noexcept
    -> void {
    destination.insert(
        destination.end(),
        std::make_move_iterator(source.begin()),
        std::make_move_iterator(source.end())
    );
}

auto declaration_origin(const SemIRProgram& semantic, DeclarationRef declaration) noexcept
    -> ProgramOriginID {
    return std::visit(
        Overloaded {
            [&](FunctionID id) noexcept { return semantic.declarations().function(id).origin; },
            [&](StructID id) noexcept { return semantic.declarations().structure(id).origin; },
            [&](EnumID id) noexcept { return semantic.declarations().enumeration(id).origin; },
            [&](ModuleConstantID id) noexcept {
                return semantic.declarations().module_constant(id).origin;
            },
        },
        declaration
    );
}

auto exported_signature(const ModuleLowering& context, FunctionID id) noexcept
    -> const CallableSignature& {
    const auto& function = context.semantic().declarations().function(id);
    const auto& callable = context.semantic().declarations().callable(function.callable);
    const auto& signature = context.semantic().callable_signatures().signature(callable.signature);
    if (!function.cpp_export_origin.has_value()
        || !context.plan().failure_abi().members(signature.failures).empty()) {
        invariant_violation("C++ export lowering received an invalid signature");
    }
    return signature;
}

} // namespace

auto lower_declaration(
    ModuleLowering& context,
    DeclarationRef declaration,
    bool declaration_only
) noexcept -> std::vector<TargetItem> {
    if (const auto* enumeration = std::get_if<EnumID>(&declaration)) {
        if (declaration_only) {
            invariant_violation("nominal declaration requested a function-only declaration form");
        }
        return lower_enumeration(context, *enumeration);
    }
    auto value = std::visit(
        Overloaded {
            [&](FunctionID id) noexcept { return lower_function(context, id, declaration_only); },
            [&](StructID id) noexcept {
                if (declaration_only) {
                    invariant_violation("structure requested a declaration-only definition");
                }
                return lower_structure(context, id);
            },
            [](EnumID) noexcept -> TargetDecl {
                invariant_violation("enum lowering did not take its exhaustive path");
            },
            [](ModuleConstantID) static noexcept -> TargetDecl {
                invariant_violation(
                    "compile-time module constant reached target declaration lowering"
                );
            },
        },
        declaration
    );
    auto result = std::vector<TargetItem>();
    result.push_back(source_item(
        context.semantic(),
        declaration_origin(context.semantic(), declaration),
        std::move(value)
    ));
    return result;
}

auto lower_forward_declaration(ModuleLowering& context, NominalDeclarationRef declaration) noexcept
    -> TargetItem {
    auto value = std::visit(
        Overloaded {
            [&](StructID id) noexcept -> TargetDecl {
                return TargetStructForwardDecl {
                    .name = context.names().structure_identifier(id),
                };
            },
            [&](EnumID id) noexcept -> TargetDecl {
                const auto& source = context.semantic().declarations().enumeration(id);
                if (std::holds_alternative<PayloadEnumRepresentation>(source.representation)) {
                    return TargetClassForwardDecl {
                        .name = context.names().enumeration_identifier(id),
                    };
                }
                return TargetEnumForwardDecl {
                    .name = context.names().enumeration_identifier(id),
                    .underlying_type = context.lower_type(
                        std::get<NumericEnumRepresentation>(source.representation).underlying_type
                    ),
                };
            },
        },
        declaration
    );
    return compiler_item(std::move(value), TargetCompilerReason::ArtifactScaffolding);
}

auto lower_cpp_export_header_declaration(ModuleLowering& context, FunctionID function) noexcept
    -> TargetItem {
    const auto& source = context.semantic().declarations().function(function);
    const auto& signature = exported_signature(context, function);
    auto parameters = std::vector<TargetParameter>();
    for (const auto& parameter : signature.parameters) {
        parameters.push_back({
            .name = std::nullopt,
            .type = context.lower_parameter(parameter),
        });
    }
    return expansion_item(
        context.semantic(),
        *source.cpp_export_origin,
        TargetDecl {TargetFunctionDecl {
            .name = TargetName {context.names()
                                    .module_names(context.names().callable_owner(source.callable))
                                    .public_functions.at(function)},
            .parameters = std::move(parameters),
            .result = context.lower_type(signature.result),
            .form = TargetFreeFunctionDeclaration {},
            .static_specifier = false,
            .inline_specifier = false,
        }}
    );
}

auto lower_cpp_export_facade(ModuleLowering& context, FunctionID function) noexcept -> TargetItem {
    const auto& source = context.semantic().declarations().function(function);
    const auto& callable = context.semantic().declarations().callable(source.callable);
    const auto& semantic_signature =
        context.semantic().callable_signatures().signature(callable.signature);
    const auto& signature = exported_signature(context, function);
    auto names = context.make_callable_name_allocator();
    auto parameters = std::vector<TargetParameter>();
    auto arguments = std::vector<TargetExpr>();
    for (auto index = 0uz; index < signature.parameters.size(); ++index) {
        const auto name = names.fresh(TargetTemporaryNameKind::CppBoundaryParameter);
        parameters.push_back({
            .name = name,
            .type = context.lower_parameter(signature.parameters[index]),
        });
        auto argument = name_expression(name);
        if (is_char_type(context.semantic(), semantic_signature.parameters[index].type)) {
            argument = call_expression(
                intrinsic_expression(TargetSymbol::RuntimeCheckedUnicodeScalar),
                target_expressions(std::move(argument))
            );
        }
        arguments.push_back(std::move(argument));
    }
    auto call = call_expression(
        name_expression(context.global_function_name(function)),
        std::move(arguments)
    );
    auto body = std::vector<TargetStmt>();
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
    return expansion_item(
        context.semantic(),
        *source.cpp_export_origin,
        TargetDecl {TargetFunctionDecl {
            .name = TargetName {context.names()
                                    .module_names(context.names().callable_owner(source.callable))
                                    .public_functions.at(function)},
            .parameters = std::move(parameters),
            .result = context.lower_type(signature.result),
            .form = TargetFreeFunctionDefinition {.body = std::move(body)},
            .static_specifier = false,
            .inline_specifier = false,
        }}
    );
}

auto lower_module_schedule(ModuleLowering& context, const TargetModuleSchedule& schedule) noexcept
    -> LoweredModuleSchedule {
    if (schedule.module_id != context.active_module()) {
        invariant_violation("module lowering used a schedule for another module");
    }
    auto result = LoweredModuleSchedule();
    const auto& declarations = context.semantic().declarations();
    const auto& module_decl = declarations.module_decl(schedule.module_id);
    for (const auto& fragment : module_decl.cpp_source_fragments) {
        result.source_fragments.push_back({
            .value =
                TargetRawFragment {
                    .bytes =
                        std::string(context.semantic().provenance().slice(fragment.payload_origin)),
                },
            .attribution = TargetRawSourceAttribution {
                .origin =
                    target_source_origin(context.semantic().provenance(), fragment.payload_origin),
            },
        });
    }
    for (const auto nominal : schedule.private_nominal_order) {
        append_items(
            result.private_items,
            lower_declaration(
                context,
                std::visit(
                    []<typename ID>(ID id) noexcept -> DeclarationRef {
                        static_assert(std::same_as<ID, StructID> || std::same_as<ID, EnumID>);
                        return id;
                    },
                    nominal
                ),
                false
            )
        );
    }
    for (const auto callable : schedule.closure_definitions) {
        const auto type_name = context.closure_type_name(callable);
        result.private_items.push_back(compiler_item(
            TargetDecl {TargetStructForwardDecl {
                .name = type_name.components().back(),
            }},
            TargetCompilerReason::ArtifactScaffolding
        ));
    }
    auto functions = std::vector<FunctionID>();
    for (const auto item : module_decl.items) {
        const auto* function = std::get_if<FunctionID>(&item);
        if (function == nullptr) {
            continue;
        }
        functions.push_back(*function);
        if (declarations.function(*function).visibility == DeclarationVisibility::Module) {
            append_items(
                result.private_items,
                lower_declaration(context, DeclarationRef {*function}, true)
            );
        }
    }
    for (const auto callable : schedule.closure_definitions) {
        result.private_items.push_back(lower_closure_definition(context, callable));
    }
    for (const auto function : functions) {
        const auto& declaration = declarations.function(function);
        auto& target = declaration.visibility == DeclarationVisibility::Module
            ? result.private_items
            : result.module_items;
        append_items(target, lower_declaration(context, DeclarationRef {function}, false));
        if (declaration.cpp_export_origin.has_value()) {
            result.cpp_export_facades.push_back(lower_cpp_export_facade(context, function));
        }
        if (declaration.entry_point.has_value()) {
            if (result.entry_wrapper.has_value()) {
                invariant_violation("module lowering observed multiple entry points");
            }
            result.entry_wrapper = lower_entry_wrapper(context, function);
        }
    }
    if (!schedule.emitted_tests.empty()) {
        auto tests = std::vector<TargetItem>();
        for (const auto test : schedule.emitted_tests) {
            tests.push_back(lower_test(context, test));
        }
        result.module_items.push_back(
            namespace_item(std::nullopt, std::move(tests), TargetCompilerReason::TestHarness)
        );
        result.module_items.push_back(lower_module_test_runner(context, schedule.emitted_tests));
    }
    return result;
}

auto lower_entry_wrapper(ModuleLowering& context, FunctionID function_id) noexcept -> TargetItem {
    const auto& function = context.semantic().declarations().function(function_id);
    if (!function.entry_point.has_value()) {
        invariant_violation("entry wrapper lowering received a non-entry function");
    }
    const auto with_arguments = *function.entry_point == EntryPointKind::WithArguments;
    auto arguments = std::vector<TargetExpr>();
    if (with_arguments) {
        arguments.push_back(call_expression(
            intrinsic_expression(TargetSymbol::RuntimeEntryArgs),
            target_expressions(
                name_expression(TargetNameAllocator::process_argument_count()),
                name_expression(TargetNameAllocator::process_argument_vector())
            )
        ));
    }
    auto body = std::vector<TargetStmt>();
    body.push_back(generated_statement(
        TargetExprStmt {
            .expression = call_expression(
                name_expression(context.global_function_name(function_id)),
                std::move(arguments)
            ),
        }
    ));
    body.push_back(generated_statement(
        TargetReturnStmt {
            .expression = integer_expression(0),
        }
    ));
    return lower_process_entry(context, with_arguments, std::move(body));
}
