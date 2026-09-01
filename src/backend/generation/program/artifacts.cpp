module carven:backend.generation.program.artifacts.impl;

import :artifacts;
import :backend.generation.program.construction;
import :backend.generation.program.references;
import :semantic.hir.decl;
import :semantic.hir.symbol;
import :semantic.visibility;
import :support.graph;
import :support.invariant;
import :support.visit;
import std;

namespace {

auto target_nominal_order(const SemanticProgram& semantic) noexcept
    -> std::vector<HIRNominalDeclRef> {
    const auto ordinal = [&](HIRNominalDeclRef nominal) noexcept -> std::size_t {
        return std::visit(
            Overloaded {
                [](StructID id) static noexcept -> std::size_t { return id.index(); },
                [&](EnumID id) noexcept -> std::size_t {
                    return semantic.structures().size() + id.index();
                },
            },
            nominal
        );
    };
    auto source_order = std::vector<HIRNominalDeclRef>();
    source_order.reserve(semantic.structures().size() + semantic.enumerations().size());
    for (auto index = 0uz; index < semantic.structures().size(); ++index) {
        source_order.push_back(StructID::from_index(static_cast<std::uint32_t>(index)));
    }
    for (auto index = 0uz; index < semantic.enumerations().size(); ++index) {
        source_order.push_back(EnumID::from_index(static_cast<std::uint32_t>(index)));
    }
    auto visited = std::flat_set<HIRNominalDeclRef>();
    auto active = std::flat_set<HIRNominalDeclRef>();
    auto result = std::vector<HIRNominalDeclRef>();
    const auto append = [&](this const auto& self, HIRNominalDeclRef nominal) noexcept -> void {
        if (visited.contains(nominal)) {
            return;
        }
        if (!active.insert(nominal).second) {
            invariant_violation("semantic nominal containment contains a cycle");
        }
        auto dependencies = std::vector<HIRNominalDeclRef>(
            semantic.nominal_containment(nominal).begin(),
            semantic.nominal_containment(nominal).end()
        );
        std::ranges::sort(dependencies, {}, ordinal);
        for (const auto dependency : dependencies) {
            self(dependency);
        }
        active.erase(nominal);
        visited.insert(nominal);
        result.push_back(nominal);
    };
    for (const auto nominal : source_order) {
        append(nominal);
    }
    return result;
}
auto include_directive(std::string_view logical_path) noexcept -> TargetDirective {
    return {.bytes = std::format("#include <{}>", logical_path)};
}

} // namespace
auto TargetProgramBuilder::build_artifacts() noexcept -> void {
    if (!name_allocation.has_value()) {
        invariant_violation("target artifact construction requires completed target names");
    }

    const auto ordered_nominals = target_nominal_order(semantic);
    auto module_schedules = std::vector<TargetModuleSchedule>();
    auto surface_declarations =
        std::vector<std::vector<HIRDeclarationRef>>(semantic.modules().size());
    module_schedules.reserve(semantic.modules().size());
    for (auto index = 0uz; index < semantic.modules().size(); ++index) {
        module_schedules.push_back({
            .module_id = ProgramModuleID::from_index(static_cast<std::uint32_t>(index)),
            .implementation_nominal_order = {},
            .cpp_preamble_items = {},
            .private_function_declarations = {},
            .function_definitions = {},
            .entry_point = std::nullopt,
            .emitted_tests = {},
        });
    }
    for (const auto nominal : ordered_nominals) {
        const auto declaration = target_declaration_ref(nominal);
        const auto module_id = target_owner_module(semantic, declaration);
        if (target_visibility(semantic, declaration) != DeclarationVisibility::Module) {
            surface_declarations[module_id.index()].push_back(declaration);
        } else {
            module_schedules[module_id.index()].implementation_nominal_order.push_back(nominal);
        }
    }
    for (auto index = 0uz; index < semantic.modules().size(); ++index) {
        const auto module_id = ProgramModuleID::from_index(static_cast<std::uint32_t>(index));
        for (const auto& [item_index, item] :
             std::views::enumerate(semantic.hir_module(module_id).items)) {
            if (std::holds_alternative<HIRCppRegion>(item)) {
                module_schedules[index].cpp_preamble_items.push_back(
                    static_cast<std::uint32_t>(item_index)
                );
            }
            const auto* function = std::get_if<FunctionID>(&item);
            if (function != nullptr
                && semantic.function(*function).visibility != DeclarationVisibility::Module) {
                surface_declarations[index].push_back(HIRDeclarationRef {*function});
            }
            if (function != nullptr) {
                module_schedules[index].function_definitions.push_back(*function);
                if (semantic.function(*function).visibility == DeclarationVisibility::Module) {
                    module_schedules[index].private_function_declarations.push_back(*function);
                }
                if (semantic.function(*function).entry_point.has_value()) {
                    module_schedules[index].entry_point = *function;
                }
            }
            if (request.tests != TestEmissionMode::None) {
                if (const auto* test = std::get_if<TestID>(&item)) {
                    module_schedules[index].emitted_tests.push_back(*test);
                }
            }
        }
    }

    auto references = collect_target_references(semantic, surface_declarations);
    auto complete_dependencies =
        std::vector<std::flat_set<ProgramModuleID>>(semantic.modules().size());
    for (auto index = 0uz; index < references.surface_requirements.size(); ++index) {
        const auto module_id = ProgramModuleID::from_index(static_cast<std::uint32_t>(index));
        for (const auto& [nominal, requirement] : references.surface_requirements[index]) {
            if (requirement != TargetTypeCompleteness::CompleteDefinition) {
                continue;
            }
            const auto dependency = target_owner_module(semantic, target_declaration_ref(nominal));
            if (dependency != module_id) {
                complete_dependencies[index].insert(dependency);
            }
        }
    }

    const auto module_path = [&](ProgramModuleID id) noexcept -> std::string_view {
        return semantic.provenance().module_record(id).path.value();
    };
    auto surface_modules = std::vector<ProgramModuleID>();
    for (auto index = 0uz; index < module_schedules.size(); ++index) {
        if (!surface_declarations[index].empty()) {
            surface_modules.push_back(
                ProgramModuleID::from_index(static_cast<std::uint32_t>(index))
            );
        }
    }
    std::ranges::sort(surface_modules, {}, module_path);
    auto surface_index = std::vector<std::optional<std::uint32_t>>(semantic.modules().size());
    for (auto index = 0uz; index < surface_modules.size(); ++index) {
        surface_index[surface_modules[index].index()] = static_cast<std::uint32_t>(index);
    }
    auto adjacency = std::vector<std::vector<std::uint32_t>>(surface_modules.size());
    for (auto index = 0uz; index < surface_modules.size(); ++index) {
        for (const auto dependency : complete_dependencies[surface_modules[index].index()]) {
            if (!surface_index[dependency.index()].has_value()) {
                invariant_violation("complete interface dependency has no published surface");
            }
            adjacency[index].push_back(*surface_index[dependency.index()]);
        }
    }

    const auto strong_components = strongly_connected_components(std::move(adjacency));
    auto component_members = std::vector<std::vector<ProgramModuleID>>();
    component_members.reserve(strong_components.dependency_first.size());
    auto module_component = std::vector<std::optional<std::size_t>>(semantic.modules().size());
    for (auto component_index = 0uz; component_index < strong_components.dependency_first.size();
         ++component_index) {
        auto members = std::vector<ProgramModuleID>();
        for (const auto member : strong_components.dependency_first[component_index]) {
            const auto module_id = surface_modules[member];
            members.push_back(module_id);
            module_component[module_id.index()] = component_index;
        }
        component_members.push_back(std::move(members));
    }

    auto component_predecessors = std::vector<std::flat_set<std::size_t>>(component_members.size());
    for (auto component_index = 0uz; component_index < component_members.size();
         ++component_index) {
        for (const auto member : component_members[component_index]) {
            for (const auto dependency : complete_dependencies[member.index()]) {
                if (!module_component[dependency.index()].has_value()) {
                    invariant_violation("complete interface dependency has no published surface");
                }
                if (*module_component[dependency.index()] != component_index) {
                    component_predecessors[component_index].insert(
                        *module_component[dependency.index()]
                    );
                }
            }
        }
    }

    auto nominal_ordinal = std::flat_map<HIRNominalDeclRef, std::size_t>();
    for (auto index = 0uz; index < ordered_nominals.size(); ++index) {
        nominal_ordinal.emplace(ordered_nominals[index], index);
    }

    artifacts.reserve(component_members.size() + module_schedules.size() + 1uz);
    for (auto component_index = 0uz; component_index < component_members.size();
         ++component_index) {
        const auto& members = component_members[component_index];
        auto included_components = std::vector<bool>(component_members.size());
        const auto include_predecessors = [&](this const auto& self,
                                              std::size_t component) noexcept -> void {
            for (const auto predecessor : component_predecessors[component]) {
                if (included_components[predecessor]) {
                    continue;
                }
                included_components[predecessor] = true;
                self(predecessor);
            }
        };
        include_predecessors(component_index);

        auto forward_nominals = std::flat_set<HIRNominalDeclRef>();
        for (const auto member : members) {
            for (const auto& declaration : references.surface_declarations[member.index()]) {
                const auto current = std::visit(
                    Overloaded {
                        [](FunctionID) static noexcept -> std::optional<HIRNominalDeclRef> {
                            return std::nullopt;
                        },
                        [](StructID id) static noexcept -> std::optional<HIRNominalDeclRef> {
                            return HIRNominalDeclRef {id};
                        },
                        [](EnumID id) static noexcept -> std::optional<HIRNominalDeclRef> {
                            return HIRNominalDeclRef {id};
                        },
                    },
                    declaration.declaration
                );
                if (!current.has_value()) {
                    continue;
                }
                for (const auto& [nominal, requirement] : declaration.requirements) {
                    const auto owner =
                        target_owner_module(semantic, target_declaration_ref(nominal));
                    if (!module_component[owner.index()].has_value()
                        || *module_component[owner.index()] != component_index
                        || nominal_ordinal.at(nominal) <= nominal_ordinal.at(*current)) {
                        continue;
                    }
                    if (requirement == TargetTypeCompleteness::CompleteDefinition) {
                        invariant_violation(
                            "target nominal order violates a complete-definition dependency"
                        );
                    }
                    forward_nominals.insert(nominal);
                }
            }
            for (const auto& [nominal, requirement] :
                 references.surface_requirements[member.index()]) {
                const auto owner = target_owner_module(semantic, target_declaration_ref(nominal));
                if (!module_component[owner.index()].has_value()) {
                    invariant_violation("interface dependency has no published surface");
                }
                const auto owner_component = *module_component[owner.index()];
                if (owner_component == component_index || included_components[owner_component]) {
                    continue;
                }
                if (requirement == TargetTypeCompleteness::CompleteDefinition) {
                    invariant_violation("complete interface dependency was not included");
                }
                forward_nominals.insert(nominal);
            }
        }

        auto ordered_forwards =
            std::vector<HIRNominalDeclRef>(forward_nominals.begin(), forward_nominals.end());
        std::ranges::sort(
            ordered_forwards,
            [&](HIRNominalDeclRef left, HIRNominalDeclRef right) noexcept {
                const auto left_module =
                    target_owner_module(semantic, target_declaration_ref(left));
                const auto right_module =
                    target_owner_module(semantic, target_declaration_ref(right));
                if (module_path(left_module) != module_path(right_module)) {
                    return module_path(left_module) < module_path(right_module);
                }
                return nominal_ordinal.at(left) < nominal_ordinal.at(right);
            }
        );
        auto forward_declarations = std::vector<TargetInterfaceForwardDeclaration>();
        forward_declarations.reserve(ordered_forwards.size());
        for (const auto nominal : ordered_forwards) {
            forward_declarations.push_back({
                .module_id = target_owner_module(semantic, target_declaration_ref(nominal)),
                .declaration = nominal,
            });
        }

        auto declarations = std::vector<TargetInterfaceDeclaration>();
        for (const auto nominal : ordered_nominals) {
            const auto declaration = target_declaration_ref(nominal);
            const auto owner = target_owner_module(semantic, declaration);
            if (module_component[owner.index()] == component_index
                && target_visibility(semantic, declaration) != DeclarationVisibility::Module) {
                declarations.push_back({.module_id = owner, .declaration = declaration});
            }
        }
        for (const auto member : members) {
            for (const auto& item : semantic.hir_module(member).items) {
                const auto* function = std::get_if<FunctionID>(&item);
                if (function != nullptr
                    && semantic.function(*function).visibility != DeclarationVisibility::Module) {
                    declarations.push_back({
                        .module_id = member,
                        .declaration = HIRDeclarationRef {*function},
                    });
                }
            }
        }

        auto directive_groups = std::vector<TargetArtifactDirectiveGroup> {
            {.directives = {TargetDirective {.bytes = "#pragma once"}}},
            {.directives = {include_directive("carven/runtime/runtime.hpp")}},
        };
        directive_groups.reserve(
            directive_groups.size() + component_predecessors[component_index].size()
        );
        for (const auto predecessor : component_predecessors[component_index]) {
            if (predecessor >= artifacts.size()) {
                invariant_violation("interface dependency was not built dependency-first");
            }
            const auto dependency =
                TargetArtifactID::from_index(static_cast<std::uint32_t>(predecessor));
            directive_groups.push_back({
                .directives = {TargetArtifactIncludeDirective {.artifact = dependency}},
            });
        }
        const auto& anchor_path = semantic.provenance().module_record(members.front()).path;
        artifacts.push_back({
            .logical_path = interface_component_logical_path(anchor_path.components()),
            .role = GeneratedArtifactRole::Interface,
            .source_mapping = ArtifactSourceMappingPolicy::StableInterface,
            .directive_groups = std::move(directive_groups),
            .schedule = TargetInterfaceSchedule {
                .component_members = members,
                .forward_declarations = std::move(forward_declarations),
                .declarations = std::move(declarations),
            },
        });
    }

    for (auto index = 0uz; index < module_schedules.size(); ++index) {
        const auto module_id = ProgramModuleID::from_index(static_cast<std::uint32_t>(index));
        auto used_components = std::flat_set<std::size_t>();
        if (module_component[index].has_value()) {
            used_components.insert(*module_component[index]);
        }
        for (const auto dependency : references.implementation_dependencies[index]) {
            if (module_component[dependency.index()].has_value()) {
                used_components.insert(*module_component[dependency.index()]);
            }
        }
        auto directives = std::vector<TargetArtifactDirective> {
            include_directive("carven/runtime/runtime.hpp"),
        };
        directives.reserve(directives.size() + used_components.size() + 1uz);
        for (const auto component : used_components) {
            const auto dependency =
                TargetArtifactID::from_index(static_cast<std::uint32_t>(component));
            directives.push_back(TargetArtifactIncludeDirective {.artifact = dependency});
        }
        if (!module_schedules[index].emitted_tests.empty()) {
            directives.push_back(include_directive("carven/std/testing/testing.hpp"));
        }
        const auto& path = semantic.provenance().module_record(module_id).path;
        artifacts.push_back({
            .logical_path = module_implementation_logical_path(path.components()),
            .role = GeneratedArtifactRole::ModuleImplementation,
            .source_mapping = ArtifactSourceMappingPolicy::SourceAttributed,
            .directive_groups = {{.directives = std::move(directives)}},
            .schedule = std::move(module_schedules[index]),
        });
    }

    if (request.tests == TestEmissionMode::DefaultRunner) {
        artifacts.push_back({
            .logical_path = "carven-test-main.cpp",
            .role = GeneratedArtifactRole::TestEntry,
            .source_mapping = ArtifactSourceMappingPolicy::SourceAttributed,
            .directive_groups = {{
                .directives = {include_directive("carven/std/testing/testing.hpp")},
            }},
            .schedule = TargetTestEntrySchedule {},
        });
    }
}
