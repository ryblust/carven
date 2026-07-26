module carven:backend.lower.impl;

import :backend.lower;
import :backend.generation.plan;
import :backend.lowering.program;
import :backend.lowering.declarations;
import :backend.lowering.expressions;
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

auto contains_tests(std::span<const HIRModuleItem> items) noexcept -> bool {
    return std::ranges::any_of(items, [](const HIRModuleItem& item) static noexcept {
        return std::holds_alternative<TestID>(item);
    });
}

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
    TargetGenerationContext& context,
    const TargetGenerationPlan& plan,
    std::vector<TargetItemID> items
) noexcept -> std::vector<TargetItemID> {
    if (items.empty()) {
        return {};
    }
    const auto domain = append_namespace(
        context.target(),
        plan.domain_namespace(),
        std::move(items),
        TargetVerticalSeparation::Line
    );
    const auto generated = append_namespace(
        context.target(),
        plan.generated_namespace(),
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
                const auto& structure = context.semantic().structure(structure_id);
                return TargetStructForwardDecl {
                    .name = context.entity_identifier(structure.symbol),
                };
            },
            [&](EnumID enumeration_id) noexcept -> TargetDecl {
                const auto& enumeration = context.semantic().enumeration(enumeration_id);
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
    TargetGenerationContext& context,
    const TargetGenerationPlan& plan,
    const TargetInterfaceComponentPlan& component
) noexcept -> TargetInterfaceComponentUnit {
    auto header_items = std::vector<TargetItemID>();
    auto append_module_namespace = [&](ProgramModuleID module_id,
                                       std::vector<TargetItemID> declarations) noexcept {
        auto module_lowerer = context.module_lowerer(module_id);
        header_items.push_back(append_namespace(
            module_lowerer.target(),
            plan.module_plan(module_id).module_namespace_name,
            std::move(declarations),
            TargetVerticalSeparation::BlankLine
        ));
    };
    auto active_module = std::optional<ProgramModuleID>();
    auto declarations = std::vector<TargetItemID>();
    for (const auto& declaration_plan : component.forward_declarations) {
        if (active_module.has_value() && *active_module != declaration_plan.module_id) {
            append_module_namespace(*active_module, std::move(declarations));
            declarations.clear();
        }
        active_module = declaration_plan.module_id;
        auto module_lowerer = context.module_lowerer(declaration_plan.module_id);
        declarations.push_back(
            lower_forward_declaration(module_lowerer, declaration_plan.declaration)
        );
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
    for (const auto& declaration_plan : component.declarations) {
        if (active_module.has_value() && *active_module != declaration_plan.module_id) {
            append_declarations(*active_module, planned_declarations);
            planned_declarations.clear();
        }
        active_module = declaration_plan.module_id;
        planned_declarations.push_back(declaration_plan.declaration);
    }
    if (active_module.has_value()) {
        append_declarations(*active_module, planned_declarations);
    }
    return TargetInterfaceComponentUnit {
        .logical_path = component.logical_path,
        .prerequisite_header_paths = component.prerequisite_header_paths,
        .sections = {
            .preamble = {},
            .body = wrap_linkage_namespaces(context, plan, std::move(header_items)),
            .epilogue = {},
        },
    };
}

auto lower_module(
    TargetGenerationContext& context,
    ProgramModuleID module_id,
    const TargetGenerationPlan& generation_plan
) noexcept -> TargetModuleImplementationUnit {
    const auto& hir_module = context.semantic().modules()[module_id.index()];
    auto module_lowerer = context.module_lowerer(module_id);
    const auto& module_plan = generation_plan.module_plan(module_id);
    auto root_items = std::vector<TargetItemID>();
    auto declaration_schedule = lower_declaration_schedule(
        module_lowerer,
        hir_module.items,
        module_plan.surface_declarations,
        module_plan.implementation_nominal_order
    );
    auto implementation_declarations = std::move(declaration_schedule.implementation);
    const auto entry_point = declaration_schedule.entry_point;
    auto preamble = std::move(declaration_schedule.cpp_preamble);
    if (!implementation_declarations.empty()) {
        root_items.push_back(append_namespace(
            module_lowerer.target(),
            module_plan.module_namespace_name,
            std::move(implementation_declarations),
            TargetVerticalSeparation::BlankLine
        ));
    }
    auto epilogue = std::vector<TargetItemID>();
    if (entry_point.has_value()) {
        const auto& function = context.semantic().function(*entry_point);
        epilogue.push_back(
            lower_entry_wrapper(module_lowerer, function, module_plan.qualified_namespace_name)
        );
    }
    return TargetModuleImplementationUnit {
        .logical_path = module_plan.implementation_logical_path,
        .interface_header_paths = module_plan.interface_header_paths,
        .testing_support = context.emits_tests() && contains_tests(hir_module.items),
        .sections = {
            .preamble = std::move(preamble),
            .body = wrap_linkage_namespaces(context, generation_plan, std::move(root_items)),
            .epilogue = std::move(epilogue),
        },
    };
}

} // namespace

auto lower_interface_component_unit(
    const SemanticProgram& semantic,
    TestEmissionMode test_mode,
    const TargetGenerationPlan& plan,
    const TargetInterfaceComponentPlan& component
) noexcept -> TargetUnit {
    auto context = TargetGenerationContext(semantic, plan, test_mode);
    auto root = lower_interface(context, plan, component);
    return std::move(context).finish(std::move(root));
}

auto lower_module_implementation_unit(
    const SemanticProgram& semantic,
    TestEmissionMode test_mode,
    const TargetGenerationPlan& plan,
    ProgramModuleID module_id
) noexcept -> TargetUnit {
    auto context = TargetGenerationContext(semantic, plan, test_mode);
    auto root = lower_module(context, module_id, plan);
    return std::move(context).finish(std::move(root));
}

auto lower_test_entry_unit(
    const SemanticProgram& semantic,
    TestEmissionMode test_mode,
    const TargetGenerationPlan& plan
) noexcept -> TargetUnit {
    auto context = TargetGenerationContext(semantic, plan, test_mode);
    auto entry_lowerer = context.module_lowerer(ProgramModuleID::from_index(0));
    const auto call = call_expression(
        entry_lowerer,
        name_expression(entry_lowerer, TargetSymbol::TestingRun),
        {}
    );
    const auto returned =
        entry_lowerer.target().append_lowering_statement(TargetReturnStmt {.expression = call});
    const auto entry = lower_process_entry(entry_lowerer, false, {returned});
    return std::move(context).finish(
        TargetTestEntryUnit {
            .logical_path = "carven-test-main.cpp",
            .items = {entry},
        }
    );
}
