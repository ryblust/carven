module carven:backend.lowering.decl.impl;

import :backend.generation.names;
import :backend.generation.plan;
import :backend.lowering.context;
import :backend.lowering.decl.lowerer;
import :backend.lowering.decl;
import :backend.target.builder;
import :backend.target.decl;
import :backend.target.expr;
import :backend.target.item;
import :backend.target.raw;
import :backend.target.stmt;
import :backend.target.symbol;
import :backend.target.type;
import :backend.target.unit;
import :semantic.semir.decl;
import :semantic.semir.ids;
import :semantic.semir.program;
import :semantic.semir.type;
import :source.provenance.ids;
import :source.provenance;
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
    return declaration.visit(
        Overloaded {
            [&](FunctionID id) noexcept { return semantic.declarations().function(id).origin; },
            [&](StructID id) noexcept { return semantic.declarations().structure(id).origin; },
            [&](EnumID id) noexcept { return semantic.declarations().enumeration(id).origin; },
            [&](ModuleConstantID id) noexcept {
                return semantic.declarations().module_constant(id).origin;
            },
        }
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
    auto value = declaration.visit(
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
        }
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
    auto value = declaration.visit(
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
        }
    );
    return compiler_item(std::move(value), TargetCompilerReason::ArtifactScaffolding);
}

auto lower_cpp_export_header_declaration(ModuleLowering& context, FunctionID function) noexcept
    -> TargetItem {
    const auto& source = context.semantic().declarations().function(function);
    const auto& signature = exported_signature(context, function);
    auto parameters = std::vector<TargetParameter>();
    for (const auto& parameter : signature.parameters) {
        parameters.push_back(
            {.local = std::nullopt,
             .type = context.lower_parameter(parameter),
             .default_value = std::nullopt}
        );
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
            .constexpr_specifier = false,
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
        const auto name =
            context.target().add_local(names.fresh(TargetTemporaryNameKind::CppBoundaryParameter));
        parameters.push_back(
            {.local = name,
             .type = context.lower_parameter(signature.parameters[index]),
             .default_value = std::nullopt}
        );
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
    if (context.semantic().may_stop_test(source.callable)) {
        call = call_expression(
            intrinsic_expression(TargetSymbol::RuntimeUnwrapNativeResult),
            target_expressions(std::move(call))
        );
    }
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
            .constexpr_specifier = false,
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
            result.private_declarations,
            lower_declaration(
                context,
                nominal.visit([]<typename ID>(ID id) noexcept -> DeclarationRef {
                    static_assert(std::same_as<ID, StructID> || std::same_as<ID, EnumID>);
                    return id;
                }),
                false
            )
        );
    }
    auto functions = std::flat_map<CallableID, FunctionID>();
    for (const auto item : module_decl.items) {
        const auto* function = std::get_if<FunctionID>(&item);
        if (function == nullptr) {
            continue;
        }
        const auto& declaration = declarations.function(*function);
        functions.emplace(declaration.callable, *function);
        if (declaration.visibility != DeclarationVisibility::Module) {
            context.require_callable(declaration.callable);
        }
        if (declaration.cpp_export_origin.has_value()) {
            result.cpp_export_facades.push_back(lower_cpp_export_facade(context, *function));
        }
        if (declaration.entry_point.has_value()) {
            if (result.entry_wrapper.has_value()) {
                invariant_violation("module lowering observed multiple entry points");
            }
            result.entry_wrapper = lower_entry_wrapper(context, *function);
        }
    }
    auto tests = std::vector<TargetItem>();
    for (const auto test : schedule.emitted_tests) {
        tests.push_back(lower_test(context, test));
    }
    for (const auto callable : schedule.interface_closures) {
        context.require_callable(callable);
    }

    auto function_declarations = std::vector<TargetItem>();
    auto function_definitions = std::flat_map<FunctionID, std::vector<TargetItem>>();
    auto closure_types = std::flat_map<CallableID, TargetItem>();
    auto closure_bodies = std::flat_map<CallableID, TargetItem>();
    while (const auto callable = context.next_required_callable()) {
        if (const auto found = functions.find(*callable); found != functions.end()) {
            const auto function = found->second;
            if (declarations.function(function).visibility == DeclarationVisibility::Module) {
                append_items(
                    function_declarations,
                    lower_declaration(context, DeclarationRef {function}, true)
                );
            }
            function_definitions.emplace(
                function,
                lower_declaration(context, DeclarationRef {function}, false)
            );
        } else {
            if (!std::ranges::contains(schedule.closure_definitions, *callable)) {
                invariant_violation("required callable is outside the module schedule");
            }
            if (!std::ranges::contains(schedule.interface_closures, *callable)) {
                closure_types.emplace(*callable, lower_closure_type(context, *callable));
            }
            closure_bodies.emplace(*callable, lower_closure_body(context, *callable));
        }
    }

    // Realization discovers definitions; the schedule retains declaration order.
    for (const auto callable : schedule.closure_definitions) {
        if (closure_types.contains(callable)) {
            result.private_declarations.push_back(compiler_item(
                TargetDecl {TargetStructForwardDecl {
                    .name = context.names()
                                .closure_type_name(context.active_module(), callable)
                                .components()
                                .back(),
                }},
                TargetCompilerReason::ArtifactScaffolding
            ));
        }
    }
    append_items(result.private_declarations, std::move(function_declarations));
    for (const auto callable : schedule.closure_definitions) {
        if (const auto found = closure_types.find(callable); found != closure_types.end()) {
            result.private_declarations.push_back(std::move(found->second));
        }
        if (const auto found = closure_bodies.find(callable); found != closure_bodies.end()) {
            auto& destination = std::ranges::contains(schedule.interface_closures, callable)
                ? result.module_items
                : result.private_items;
            destination.push_back(std::move(found->second));
        }
    }
    append_items(result.private_declarations, context.take_display_helpers());
    for (auto&& [function, definition] : function_definitions) {
        auto& target = declarations.function(function).visibility == DeclarationVisibility::Module
            ? result.private_items
            : result.module_items;
        append_items(target, std::move(definition));
    }
    if (!tests.empty()) {
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
    auto process_arguments = std::optional<std::array<TargetLocalID, 2>>();
    auto arguments = std::vector<TargetExpr>();
    if (with_arguments) {
        process_arguments = std::array {
            context.target().add_local(TargetNameAllocator::process_argument_count()),
            context.target().add_local(TargetNameAllocator::process_argument_vector())
        };
        arguments.push_back(call_expression(
            intrinsic_expression(TargetSymbol::RuntimeEntryArgs),
            target_expressions(
                name_expression((*process_arguments)[0]),
                name_expression((*process_arguments)[1])
            )
        ));
    }
    auto body = std::vector<TargetStmt>();
    auto call = call_expression(
        name_expression(context.global_function_name(function_id)),
        std::move(arguments)
    );
    const auto& callable = context.semantic().declarations().callable(function.callable);
    const auto& signature = context.semantic().callable_signatures().signature(callable.signature);
    auto status = integer_expression(0);
    if (context.plan().failure_abi().members(signature.failures).empty()
        && !context.semantic().may_stop_test(function.callable)) {
        body.push_back(generated_statement(TargetExprStmt {.expression = std::move(call)}));
    } else {
        auto names = context.make_callable_name_allocator();
        const auto outcome =
            context.target().add_local(names.fresh(TargetTemporaryNameKind::Outcome));
        body.push_back(generated_statement(
            TargetVariableStmt {
                .binding = TargetVariableBinding::ConstValue,
                .maybe_unused = false,
                .local = outcome,
                .type = context.intrinsic_type(TargetSymbol::Auto),
                .initializer = std::move(call),
            }
        ));
        status = TargetExpr {
            .value = TargetConditionalExpr {
                .condition = target_child(call_expression(
                    member_expression(
                        name_expression(outcome),
                        TargetIdentifier::from_spelling("success_if")
                    ),
                    {}
                )),
                .true_value = target_child(integer_expression(0)),
                .false_value = target_child(intrinsic_expression(TargetSymbol::StdExitFailure)),
            }
        };
    }
    body.push_back(generated_statement(TargetReturnStmt {.expression = std::move(status)}));
    return lower_process_entry(context, process_arguments, std::move(body));
}
