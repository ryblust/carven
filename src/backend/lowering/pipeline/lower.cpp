module carven:backend.lower.impl;

import :artifacts;
import :backend.lower;
import :backend.generation.program;
import :backend.lowering.program;
import :backend.lowering.decl;
import :backend.lowering.expr;
import :backend.generation.names;
import :backend.lowering.names;
import :backend.lowering.types;
import :backend.target;
import :backend.target.ids;
import :backend.target.item;
import :backend.target.stmt;
import :backend.target.symbol;
import :backend.target.unit;
import :semantic.hir;
import :semantic.hir.decl;
import :semantic.hir.ids;
import :support.invariant;
import :support.visit;
import std;

namespace {

auto append_namespace(
    TargetUnitBuilder& target,
    TargetName name,
    std::vector<TargetItemID> items,
    TargetVerticalSeparation body_separation
) noexcept -> TargetItemID {
    return target.append_item({
        .value =
            TargetNamespace {
                .name = std::move(name),
                .items = std::move(items),
                .body_separation = body_separation,
            },
        .attribution = {
            .kind = TargetAttributionKind::CompilerOwned,
            .origin = std::nullopt,
            .reason = TargetSyntheticReason::ArtifactScaffolding,
        },
    });
}

auto wrap_linkage_namespaces(
    TargetUnitLoweringContext& context,
    std::vector<TargetItemID> items
) noexcept -> std::vector<TargetItemID> {
    if (items.empty()) {
        return {};
    }
    const auto domain = append_namespace(
        context.target(),
        context.source().domain_namespace(),
        std::move(items),
        TargetVerticalSeparation::Line
    );
    const auto generated = append_namespace(
        context.target(),
        context.source().generated_namespace(),
        std::vector<TargetItemID> {domain},
        TargetVerticalSeparation::Line
    );
    return {generated};
}

auto lower_forward_declaration(TargetModuleLowerer& context, HIRNominalDeclRef declaration) noexcept
    -> TargetItemID {
    auto value = std::visit(
        Overloaded {
            [&](StructID structure_id) noexcept -> TargetDecl {
                const auto& structure = context.source().structure(structure_id);
                return TargetStructForwardDecl {
                    .name = context.entity_identifier(structure.symbol),
                };
            },
            [&](EnumID enumeration_id) noexcept -> TargetDecl {
                const auto& enumeration = context.source().enumeration(enumeration_id);
                if (enumeration.profile == HIREnumProfile::Payload) {
                    return TargetClassForwardDecl {
                        .name = context.entity_identifier(enumeration.symbol),
                    };
                }
                if (!enumeration.underlying_type.has_value()) {
                    invariant_violation(
                        "numeric enumeration forward declaration has no underlying type"
                    );
                }
                return TargetEnumForwardDecl {
                    .name = context.entity_identifier(enumeration.symbol),
                    .underlying_type = lower_type(context, *enumeration.underlying_type),
                };
            },
        },
        declaration
    );
    return context.target().append_item({
        .value = std::move(value),
        .attribution = {
            .kind = TargetAttributionKind::CompilerOwned,
            .origin = std::nullopt,
            .reason = TargetSyntheticReason::ArtifactScaffolding,
        },
    });
}

auto lower_interface(
    TargetUnitLoweringContext& context,
    const TargetInterfaceSchedule& schedule
) noexcept -> TargetUnitSections {
    auto header_items = std::vector<TargetItemID>();
    const auto append_module_namespace = [&](ProgramModuleID module_id,
                                             std::vector<TargetItemID> declarations) noexcept {
        auto module_lowerer = context.module_lowerer(module_id);
        header_items.push_back(append_namespace(
            module_lowerer.target(),
            context.source().module(module_id).module_namespace_name,
            std::move(declarations),
            TargetVerticalSeparation::BlankLine
        ));
    };

    auto active_module = std::optional<ProgramModuleID>();
    auto declarations = std::vector<TargetItemID>();
    for (const auto& planned : schedule.forward_declarations) {
        if (active_module.has_value() && *active_module != planned.module_id) {
            append_module_namespace(*active_module, std::move(declarations));
            declarations.clear();
        }
        active_module = planned.module_id;
        auto module_lowerer = context.module_lowerer(planned.module_id);
        declarations.push_back(lower_forward_declaration(module_lowerer, planned.declaration));
    }
    if (active_module.has_value()) {
        append_module_namespace(*active_module, std::move(declarations));
    }

    const auto append_declarations = [&](ProgramModuleID module_id,
                                         const std::vector<HIRDeclarationRef>& planned) noexcept {
        auto module_lowerer = context.module_lowerer(module_id);
        auto items = std::vector<TargetItemID>();
        auto functions = std::vector<FunctionID>();
        for (const auto declaration : planned) {
            if (const auto* function = std::get_if<FunctionID>(&declaration)) {
                functions.push_back(*function);
            } else {
                items.push_back(lower_declaration(module_lowerer, declaration, false));
            }
        }
        auto function_declarations = std::vector<TargetItemID>();
        function_declarations.reserve(functions.size());
        for (const auto function : functions) {
            function_declarations.push_back(
                lower_declaration(module_lowerer, HIRDeclarationRef {function}, true)
            );
        }
        if (!function_declarations.empty()) {
            items.push_back(module_lowerer.target().append_item({
                .value =
                    TargetItemGroup {
                        .items = std::move(function_declarations),
                        .separation = TargetVerticalSeparation::Line,
                    },
                .attribution = {
                    .kind = TargetAttributionKind::CompilerOwned,
                    .origin = std::nullopt,
                    .reason = TargetSyntheticReason::ArtifactScaffolding,
                },
            }));
        }
        append_module_namespace(module_id, std::move(items));
    };

    active_module.reset();
    auto planned_declarations = std::vector<HIRDeclarationRef>();
    for (const auto& planned : schedule.declarations) {
        if (active_module.has_value() && *active_module != planned.module_id) {
            append_declarations(*active_module, planned_declarations);
            planned_declarations.clear();
        }
        active_module = planned.module_id;
        planned_declarations.push_back(planned.declaration);
    }
    if (active_module.has_value()) {
        append_declarations(*active_module, planned_declarations);
    }
    return {
        .preamble = {},
        .body = wrap_linkage_namespaces(context, std::move(header_items)),
        .epilogue = {},
    };
}

auto lower_module(TargetUnitLoweringContext& context, const TargetModuleSchedule& schedule) noexcept
    -> TargetUnitSections {
    const auto module_id = schedule.module_id;
    const auto& module_names = context.source().module(module_id);
    auto module_lowerer = context.module_lowerer(module_id);
    auto root_items = std::vector<TargetItemID>();
    auto declaration_schedule = lower_declaration_schedule(module_lowerer, schedule);
    auto implementation_declarations = std::move(declaration_schedule.implementation);
    const auto entry_point = declaration_schedule.entry_point;
    auto preamble = std::move(declaration_schedule.cpp_preamble);
    if (!implementation_declarations.empty()) {
        root_items.push_back(append_namespace(
            module_lowerer.target(),
            module_names.module_namespace_name,
            std::move(implementation_declarations),
            TargetVerticalSeparation::BlankLine
        ));
    }
    auto epilogue = std::vector<TargetItemID>();
    if (entry_point.has_value()) {
        const auto& function = context.source().function(*entry_point);
        epilogue.push_back(
            lower_entry_wrapper(module_lowerer, function, module_names.qualified_namespace_name)
        );
    }
    return {
        .preamble = std::move(preamble),
        .body = wrap_linkage_namespaces(context, std::move(root_items)),
        .epilogue = std::move(epilogue),
    };
}

auto lower_test_entry(TargetUnitLoweringContext& context) noexcept -> TargetUnitSections {
    auto entry_lowerer = context.module_lowerer(ProgramModuleID::from_index(0));
    const auto call = call_expression(
        entry_lowerer,
        name_expression(entry_lowerer, TargetSymbol::TestingRun),
        {}
    );
    const auto returned =
        entry_lowerer.target().append_lowering_statement(TargetReturnStmt {.expression = call});
    const auto entry = lower_process_entry(entry_lowerer, false, {returned});
    return {
        .preamble = {},
        .body = {entry},
        .epilogue = {},
    };
}

} // namespace

auto lower_target_unit(const TargetProgram& program, TargetArtifactID artifact_id) noexcept
    -> TargetUnit {
    auto context = TargetUnitLoweringContext(program.focused_artifact(artifact_id));
    const auto& artifact = context.source().artifact();
    auto sections = std::visit(
        Overloaded {
            [&](const TargetInterfaceSchedule& schedule) noexcept {
                if (artifact.role != GeneratedArtifactRole::Interface) {
                    invariant_violation("interface schedule has a non-interface artifact role");
                }
                return lower_interface(context, schedule);
            },
            [&](const TargetModuleSchedule& schedule) noexcept {
                if (artifact.role != GeneratedArtifactRole::ModuleImplementation) {
                    invariant_violation("module schedule has a non-module artifact role");
                }
                return lower_module(context, schedule);
            },
            [&](const TargetTestEntrySchedule&) noexcept {
                if (artifact.role != GeneratedArtifactRole::TestEntry) {
                    invariant_violation("test schedule has a non-test artifact role");
                }
                return lower_test_entry(context);
            },
        },
        artifact.schedule
    );
    auto directive_groups = context.source().materialize_directive_groups();
    return std::move(context).finish({
        .logical_path = artifact.logical_path,
        .role = artifact.role,
        .source_mapping = artifact.source_mapping,
        .directive_groups = std::move(directive_groups),
        .sections = std::move(sections),
    });
}
