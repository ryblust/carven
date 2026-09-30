module carven:backend.lowering.decl.impl;

import :backend.generation.names;
import :backend.generation.plan;
import :backend.lowering.context;
import :backend.lowering.decl;
import :backend.lowering.decl.lowerer;
import :backend.realization.operation;
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
import :source.provenance;
import :source.provenance.ids;
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
    if (!function.cpp_export_origin.has_value()) {
        invariant_violation("C++ export lowering received an invalid signature");
    }
    return signature;
}

auto lower_use_placed_function(
    ModuleLowering& context,
    CallableID callable,
    bool declaration_only
) noexcept -> TargetItem {
    const auto function = context.semantic().source_function(callable);
    if (!function) {
        invariant_violation("a definition placed at its uses must implement a function");
    }
    return source_item(
        context.semantic(),
        context.semantic().declarations().function(*function).origin,
        lower_carven_function(context, callable, declaration_only)
    );
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
            [](EnumID) static noexcept -> TargetDecl {
                invariant_violation("enum lowering did not take its exhaustive path");
            },
            [](ModuleConstantID) static noexcept -> TargetDecl {
                invariant_violation("module constant reached target declaration lowering");
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
             .type =
                 context.lower_parameter(parameter.access, parameter.type, TypeNameScope::Global),
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
            .result = context.lower_signature_result(
                context.semantic().declarations().callable(source.callable).signature,
                false,
                TypeNameScope::Global
            ),
            .form = TargetFreeFunctionDeclaration {},
            .constexpr_specifier = false,
            .static_specifier = false,
            .inline_specifier = false,
        }}
    );
}

auto lower_cpp_export_facade(ModuleLowering& context, FunctionID function) noexcept -> TargetItem {
    const auto& source = context.semantic().declarations().function(function);
    const auto& signature = exported_signature(context, function);
    auto names = context.make_callable_name_allocator();
    auto parameters = std::vector<TargetParameter>();
    auto arguments = std::vector<TargetExpr>();
    for (auto index = 0uz; index < signature.parameters.size(); ++index) {
        const auto name =
            context.target().add_local(names.fresh(TargetTemporaryNameKind::CppBoundaryParameter));
        parameters.push_back(
            {.local = name,
             .type = context.lower_parameter(
                 signature.parameters[index].access,
                 signature.parameters[index].type,
                 TypeNameScope::Global
             ),
             .default_value = std::nullopt}
        );
        auto argument = name_expression(name);
        if (signature.parameters[index].access != AccessMode::Write
            && is_char_type(context.semantic(), signature.parameters[index].type)) {
            argument = call_expression(
                intrinsic_expression(TargetSymbol::RuntimeCheckedUnicodeScalar),
                target_expressions(
                    std::move(argument),
                    source_site_expression(context, source.origin)
                )
            );
        }
        if (signature.parameters[index].access == AccessMode::Take
            && !is_char_type(context.semantic(), signature.parameters[index].type)) {
            argument = transfer_expression(std::move(argument));
        }
        arguments.push_back(std::move(argument));
    }
    auto call = call_expression(
        name_expression(context.global_function_name(function)),
        std::move(arguments)
    );
    if (context.semantic().may_stop_test(source.callable)) {
        call = call_expression(
            intrinsic_expression(TargetSymbol::RuntimeNativeTestResult),
            target_expressions(std::move(call))
        );
    }
    auto body = std::vector<TargetStmt>();
    body.push_back(generated_statement(TargetReturnStmt {.expression = std::move(call)}));
    return expansion_item(
        context.semantic(),
        *source.cpp_export_origin,
        TargetDecl {TargetFunctionDecl {
            .name = TargetName {context.names()
                                    .module_names(context.names().callable_owner(source.callable))
                                    .public_functions.at(function)},
            .parameters = std::move(parameters),
            .result = context.lower_signature_result(
                context.semantic().declarations().callable(source.callable).signature,
                false,
                TypeNameScope::Global
            ),
            .form = TargetFreeFunctionDefinition {.body = std::move(body)},
            .constexpr_specifier = false,
            .static_specifier = false,
            .inline_specifier = false,
        }}
    );
}

auto lower_module_schedule(
    ArtifactLowering& artifact,
    const TargetModuleSchedule& schedule
) noexcept -> LoweredModuleSchedule {
    auto& context = artifact.module_context(schedule.module_id);
    auto result = LoweredModuleSchedule {
        .source_fragments = {},
        .nominal_declarations = {},
        .private_declarations = {},
        .private_items = {},
        .module_items = {},
        .shared_declarations = {},
        .shared_definitions = {},
        .entry_wrapper = std::nullopt,
        .cpp_export_facades = {},
    };
    const auto& declarations = context.semantic().declarations();
    const auto& module_decl = declarations.module_decl(schedule.module_id);
    const auto wrap = [&](ModuleID owner, std::vector<TargetItem> items) noexcept {
        return namespace_item(
            context.names().module_names(owner).module_namespace_name,
            std::move(items),
            TargetCompilerReason::ArtifactScaffolding,
            false
        );
    };
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
        auto lowered = lower_declaration(
            context,
            nominal.visit([]<typename ID>(ID id) static noexcept -> DeclarationRef {
                static_assert(std::same_as<ID, StructID> || std::same_as<ID, EnumID>);
                return id;
            }),
            false
        );
        append_items(result.nominal_declarations, context.take_query_aliases());
        result.nominal_declarations.push_back(namespace_item(std::nullopt, std::move(lowered)));
    }
    auto functions = std::flat_map<CallableID, FunctionID>();
    for (const auto item : module_decl.items) {
        const auto* function = std::get_if<FunctionID>(&item);
        if (function == nullptr) {
            continue;
        }
        const auto& declaration = declarations.function(*function);
        functions.emplace(declaration.callable, *function);
        if (context.semantic().definition_placement(declaration.callable)
                == DefinitionPlacement::Owner
            && std::ranges::contains(schedule.interface_callables, declaration.callable)) {
            context.require_callable(declaration.callable);
        }
        if (declaration.cpp_export_origin) {
            result.cpp_export_facades.push_back(lower_cpp_export_facade(context, *function));
        }
        if (schedule.emit_program_entry && declaration.entry_point) {
            if (result.entry_wrapper) {
                invariant_violation("module lowering observed multiple entry points");
            }
            result.entry_wrapper = lower_entry_wrapper(context, *function);
        }
    }
    auto tests = std::vector<TargetItem>();
    for (const auto test : schedule.emitted_tests) {
        tests.push_back(lower_test(context, test));
    }
    for (const auto callable : schedule.interface_callables) {
        if (std::holds_alternative<ClosureBodyImplementation>(
                declarations.callable(callable).implementation
            )) {
            context.require_callable(callable);
        }
    }
    const auto externally_linked = [&](CallableID callable) noexcept {
        return std::ranges::contains(schedule.interface_callables, callable);
    };
    auto function_declarations = std::vector<TargetItem>();
    auto function_definitions = std::flat_map<FunctionID, std::vector<TargetItem>>();
    auto closure_types = std::flat_map<CallableID, TargetItem>();
    auto closure_bodies = std::flat_map<CallableID, TargetItem>();
    auto use_placed = std::map<CallableID, TargetItem>();
    while (const auto definition = artifact.next_definition()) {
        const auto callable = *definition;
        if (context.semantic().definition_placement(callable) == DefinitionPlacement::Use) {
            auto& owner = artifact.module_context(context.names().callable_owner(callable));
            use_placed.emplace(callable, lower_use_placed_function(owner, callable, false));
            continue;
        }
        if (const auto found = functions.find(callable); found != functions.end()) {
            if (!externally_linked(callable)) {
                append_items(
                    function_declarations,
                    lower_declaration(context, found->second, true)
                );
            }
            function_definitions.emplace(
                found->second,
                lower_declaration(context, found->second, false)
            );
        } else {
            if (!externally_linked(callable)) {
                closure_types.emplace(callable, lower_closure_type(context, callable));
            }
            closure_bodies.emplace(callable, lower_closure_body(context, callable));
        }
    }
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
            auto& destination =
                externally_linked(callable) ? result.module_items : result.private_items;
            destination.push_back(std::move(found->second));
        }
    }
    for (auto&& [function, definition] : function_definitions) {
        append_items(
            externally_linked(declarations.function(function).callable) ? result.module_items
                                                                        : result.private_items,
            std::move(definition)
        );
    }

    // Emitted call edges determine definition order. A back edge requires the
    // declaration of its target; acyclic edges need no declarations.
    auto state = std::map<CallableID, std::uint8_t>();
    auto forward = std::set<CallableID>();
    auto order = std::vector<CallableID>();
    const auto visit = [&](this const auto& self, CallableID id) noexcept -> void {
        if (state[id] == 2) {
            return;
        }
        if (state[id] == 1) {
            forward.insert(id);
            return;
        }
        state[id] = 1;
        for (const auto callee : artifact.use_placed_callees(id)) {
            self(callee);
        }
        state[id] = 2;
        order.push_back(id);
    };
    for (const auto& [id, definition] : use_placed) {
        static_cast<void>(definition);
        visit(id);
    }
    const auto definition_owner = [&](CallableID id) noexcept {
        return context.names().callable_owner(id);
    };
    // Consecutive items of one owner share its namespace block.
    const auto group = [&](const auto& ids, auto lower) noexcept {
        auto groups = std::vector<TargetItem>();
        for (auto begin = ids.begin(); begin != ids.end();) {
            const auto owner = definition_owner(*begin);
            auto items = std::vector<TargetItem>();
            auto end = begin;
            for (; end != ids.end() && definition_owner(*end) == owner; ++end) {
                items.push_back(lower(owner, *end));
            }
            groups.push_back(wrap(owner, std::move(items)));
            begin = end;
        }
        return groups;
    };
    result.shared_declarations = group(forward, [&](ModuleID owner, CallableID id) noexcept {
        return lower_use_placed_function(artifact.module_context(owner), id, true);
    });
    result.shared_definitions = group(order, [&](ModuleID, CallableID id) noexcept {
        return std::move(use_placed.at(id));
    });
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
            context.target().add_local(process_argument_count_identifier()),
            context.target().add_local(process_argument_vector_identifier())
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
        const auto& semantic = context.semantic();
        const auto provenance = semantic.provenance();
        for (const auto member : context.plan().failure_abi().members(signature.failures)) {
            // A nominal failure is named by its module path, as in diagnostics.
            auto name = std::string();
            const auto qualify = [&](ModuleID owner, ProgramSpellingID spelling) noexcept {
                name = std::format(
                    "{}.{}",
                    provenance
                        .module_record(semantic.declarations().module_decl(owner).provenance_module)
                        .path.value(),
                    provenance.spelling(spelling)
                );
            };
            const auto& canonical = semantic.types().type(member).value;
            if (const auto* structure = std::get_if<StructTypeValue>(&canonical)) {
                const auto& record = semantic.declarations().structure(structure->structure);
                qualify(record.module_id, record.name);
            } else if (const auto* enumeration = std::get_if<EnumTypeValue>(&canonical)) {
                const auto& record = semantic.declarations().enumeration(enumeration->enumeration);
                qualify(record.module_id, record.name);
            } else {
                invariant_violation("entry failure member is not a nominal type");
            }
            auto report = target_expressions(
                template_call_expression(
                    member_expression(
                        name_expression(outcome),
                        TargetIdentifier::from_spelling("failure_if")
                    ),
                    {context.lower_type(member, TypeNameScope::Global)},
                    {}
                ),
                string_expression(std::move(name), TargetStringLiteralKind::String),
                context.display_emitter(member)
            );
            report.push_back(source_site_expression(context, function.origin));
            body.push_back(generated_statement(
                TargetExprStmt {
                    .expression = call_expression(
                        intrinsic_expression(TargetSymbol::RuntimeReportEntryFailure),
                        std::move(report)
                    )
                }
            ));
        }
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
