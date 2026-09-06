module carven:backend.generation.plan.artifacts.impl;

import :artifacts;
import :backend.generation.plan.references;
import :backend.generation.plan;
import :backend.generation.request;
import :semantic.visibility;
import :support.graph;
import :support.invariant;
import :support.visit;
import std;

namespace {

template<typename Entries>
auto entry_count(Entries entries) noexcept -> std::size_t {
    auto result = 0uz;
    for (const auto entry : entries) {
        static_cast<void>(entry);
        ++result;
    }
    return result;
}

auto nominal_origin(const SemIRProgram& semantic, NominalDeclarationRef nominal) noexcept
    -> ProgramOriginID {
    return std::visit(
        Overloaded {
            [&](StructID id) noexcept { return semantic.declarations().structure(id).origin; },
            [&](EnumID id) noexcept { return semantic.declarations().enumeration(id).origin; },
        },
        nominal
    );
}

auto nominal_name(const SemIRProgram& semantic, NominalDeclarationRef nominal) noexcept
    -> ProgramSpellingID {
    return std::visit(
        Overloaded {
            [&](StructID id) noexcept { return semantic.declarations().structure(id).name; },
            [&](EnumID id) noexcept { return semantic.declarations().enumeration(id).name; },
        },
        nominal
    );
}

auto module_path(const SemIRProgram& semantic, ModuleID id) noexcept -> std::string_view {
    const auto& declaration = semantic.declarations().module_decl(id);
    return semantic.provenance().module_record(declaration.provenance_module).path.value();
}

auto nominal_key(const SemIRProgram& semantic, NominalDeclarationRef nominal) noexcept
    -> std::tuple<std::string, std::uint32_t, std::string, std::size_t> {
    const auto owner = target_owner_module(semantic, target_declaration_ref(nominal));
    return {
        std::string(module_path(semantic, owner)),
        semantic.provenance().location(nominal_origin(semantic, nominal)).line,
        std::string(semantic.provenance().spelling(nominal_name(semantic, nominal))),
        std::visit(
            []<typename ID>(ID id) static noexcept -> std::size_t {
                static_assert(std::same_as<ID, StructID> || std::same_as<ID, EnumID>);
                return id.index();
            },
            nominal
        ),
    };
}

auto complete_type_nominals(
    const SemIRProgram& semantic,
    TypeID type,
    std::flat_set<TypeID>& active,
    std::flat_set<NominalDeclarationRef>& result
) noexcept -> void {
    if (!active.insert(type).second) {
        invariant_violation("canonical type graph contains a structural cycle");
    }
    std::visit(
        Overloaded {
            [](const BuiltinTypeValue&) static noexcept {},
            [&](const StructTypeValue& value) noexcept {
                result.insert(NominalDeclarationRef {value.structure});
            },
            [&](const EnumTypeValue& value) noexcept {
                result.insert(NominalDeclarationRef {value.enumeration});
            },
            [&](const ArrayTypeValue& value) noexcept {
                complete_type_nominals(semantic, value.element, active, result);
            },
            [](const FunctionTypeValue&) static noexcept {},
            [](const ClosureTypeValue&) static noexcept {},
            [](const CallableViewTypeValue&) static noexcept {},
            [&](const CppTypeValue& value) noexcept {
                for (const auto argument : cpp_type_references(value)) {
                    complete_type_nominals(semantic, argument, active, result);
                }
            },
        },
        semantic.types().type(type).value
    );
    active.erase(type);
}

auto nominal_dependencies(const SemIRProgram& semantic, NominalDeclarationRef nominal) noexcept
    -> std::vector<NominalDeclarationRef> {
    auto result = std::flat_set<NominalDeclarationRef>();
    auto active_types = std::flat_set<TypeID>();
    std::visit(
        Overloaded {
            [&](StructID id) noexcept {
                for (const auto& field : semantic.declarations().structure(id).fields) {
                    complete_type_nominals(semantic, field.type, active_types, result);
                }
            },
            [&](EnumID id) noexcept {
                const auto& enumeration = semantic.declarations().enumeration(id);
                if (const auto* numeric =
                        std::get_if<NumericEnumRepresentation>(&enumeration.representation)) {
                    complete_type_nominals(
                        semantic,
                        numeric->underlying_type,
                        active_types,
                        result
                    );
                }
                for (const auto case_id : enumeration.cases) {
                    for (const auto type :
                         semantic.declarations().enum_case(case_id).payload_types) {
                        complete_type_nominals(semantic, type, active_types, result);
                    }
                }
            },
        },
        nominal
    );
    return std::vector<NominalDeclarationRef>(result.begin(), result.end());
}

auto target_nominal_order(const SemIRProgram& semantic) noexcept
    -> std::vector<NominalDeclarationRef> {
    auto source_order = std::vector<NominalDeclarationRef>();
    source_order.reserve(
        entry_count(semantic.declarations().structures())
        + entry_count(semantic.declarations().enumerations())
    );
    for (const auto structure : semantic.declarations().structures()) {
        source_order.push_back(NominalDeclarationRef {structure.id});
    }
    for (const auto enumeration : semantic.declarations().enumerations()) {
        source_order.push_back(NominalDeclarationRef {enumeration.id});
    }
    std::ranges::sort(source_order, [&](const auto left, const auto right) noexcept {
        return nominal_key(semantic, left) < nominal_key(semantic, right);
    });

    auto visited = std::flat_set<NominalDeclarationRef>();
    auto active = std::flat_set<NominalDeclarationRef>();
    auto result = std::vector<NominalDeclarationRef>();
    const auto append = [&](this const auto& self, NominalDeclarationRef nominal) noexcept -> void {
        if (visited.contains(nominal)) {
            return;
        }
        if (!active.insert(nominal).second) {
            invariant_violation("semantic nominal containment contains a cycle");
        }
        auto dependencies = nominal_dependencies(semantic, nominal);
        std::ranges::sort(dependencies, [&](const auto left, const auto right) noexcept {
            return nominal_key(semantic, left) < nominal_key(semantic, right);
        });
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

auto published_nominal(const SemIRProgram& semantic, NominalDeclarationRef nominal) noexcept
    -> bool {
    return target_visibility(semantic, target_declaration_ref(nominal))
        != DeclarationVisibility::Module;
}

} // namespace

auto verify_target_artifact_logical_paths(std::span<const std::string> logical_paths) noexcept
    -> void {
    auto paths = std::flat_set<std::string_view>();
    for (const auto& logical_path : logical_paths) {
        if (!validate_artifact_logical_path(logical_path)) {
            invariant_violation("target artifact schedule contains an invalid logical path");
        }
        if (!paths.insert(logical_path).second) {
            invariant_violation("target artifact schedule contains duplicate logical paths");
        }
    }
    for (const auto logical_path : paths) {
        for (auto separator = logical_path.find('/'); separator != std::string_view::npos;
             separator = logical_path.find('/', separator + 1)) {
            if (paths.contains(logical_path.substr(0, separator))) {
                invariant_violation(
                    "target artifact schedule contains a logical path prefix collision"
                );
            }
        }
    }
}

auto plan_artifacts(
    const SemIRProgram& semantic,
    const TargetPlanningRequest& request,
    const TargetClosureCatalog& closures,
    TargetPlanIdentity identity
) noexcept -> TargetPlanTable<TargetArtifactPlan, TargetArtifactID> {
    const auto& declarations = semantic.declarations();
    const auto provenance = semantic.provenance();
    const auto module_count = entry_count(declarations.modules());
    const auto ordered_nominals = target_nominal_order(semantic);

    auto module_ids = std::vector<ModuleID>();
    module_ids.reserve(module_count);
    auto module_by_index = std::vector<std::optional<ModuleID>>(module_count);
    auto schedules = std::vector<std::optional<TargetModuleSchedule>>(module_count);
    auto surface_declarations = std::vector<std::vector<DeclarationRef>>(module_count);
    for (const auto module_record : declarations.modules()) {
        module_ids.push_back(module_record.id);
        module_by_index[module_record.id.index()] = module_record.id;
        schedules[module_record.id.index()] = TargetModuleSchedule {
            .module_id = module_record.id,
            .private_nominal_order = {},
            .closure_definitions = {},
            .emitted_tests = {},
        };
        auto& closure_definitions = schedules[module_record.id.index()]->closure_definitions;
        closure_definitions.assign(
            closures.production(module_record.id).begin(),
            closures.production(module_record.id).end()
        );
        if (request.test_mode != TestGenerationMode::None) {
            closure_definitions.insert(
                closure_definitions.end(),
                closures.tests(module_record.id).begin(),
                closures.tests(module_record.id).end()
            );
        }
        for (const auto item : module_record.value.items) {
            std::visit(
                Overloaded {
                    [&](FunctionID id) noexcept {
                        const auto& function = declarations.function(id);
                        if (function.visibility != DeclarationVisibility::Module) {
                            surface_declarations[module_record.id.index()].push_back(id);
                        }
                    },
                    [&](StructID id) noexcept {
                        if (declarations.structure(id).visibility
                            != DeclarationVisibility::Module) {
                            surface_declarations[module_record.id.index()].push_back(id);
                        }
                    },
                    [&](EnumID id) noexcept {
                        if (declarations.enumeration(id).visibility
                            != DeclarationVisibility::Module) {
                            surface_declarations[module_record.id.index()].push_back(id);
                        }
                    },
                    [](ModuleConstantID) static noexcept {},
                    [&](TestID id) noexcept {
                        if (request.test_mode != TestGenerationMode::None) {
                            schedules[module_record.id.index()]->emitted_tests.push_back(id);
                        }
                    },
                },
                item
            );
        }
    }
    std::ranges::sort(module_ids, [&](ModuleID left, ModuleID right) noexcept {
        return module_path(semantic, left) < module_path(semantic, right);
    });

    for (const auto nominal : ordered_nominals) {
        if (published_nominal(semantic, nominal)) {
            continue;
        }
        const auto owner = target_owner_module(semantic, target_declaration_ref(nominal));
        schedules[owner.index()]->private_nominal_order.push_back(nominal);
    }

    const auto references = collect_target_references(semantic, surface_declarations);
    auto complete_dependencies = std::vector<std::flat_set<ModuleID>>(module_count);
    for (auto index = 0uz; index < module_count; ++index) {
        if (!module_by_index[index].has_value()) {
            invariant_violation("semantic module table contains a missing row");
        }
        const auto module_id = *module_by_index[index];
        for (const auto& [nominal, completeness] : references.surface_requirements[index]) {
            if (completeness != TargetTypeCompleteness::CompleteDefinition) {
                continue;
            }
            if (!published_nominal(semantic, nominal)) {
                invariant_violation("published surface requires a module-private nominal");
            }
            const auto dependency = target_owner_module(semantic, target_declaration_ref(nominal));
            if (dependency != module_id) {
                complete_dependencies[index].insert(dependency);
            }
        }
    }

    auto surface_modules = std::vector<ModuleID>();
    for (const auto module_id : module_ids) {
        if (!surface_declarations[module_id.index()].empty()) {
            surface_modules.push_back(module_id);
        }
    }
    auto surface_index = std::vector<std::optional<std::uint32_t>>(module_count);
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
    auto component_members = std::vector<std::vector<ModuleID>>();
    component_members.reserve(strong_components.dependency_first.size());
    auto module_component = std::vector<std::optional<std::size_t>>(module_count);
    for (auto component_index = 0uz; component_index < strong_components.dependency_first.size();
         ++component_index) {
        auto members = std::vector<ModuleID>();
        for (const auto member : strong_components.dependency_first[component_index]) {
            const auto module_id = surface_modules[member];
            members.push_back(module_id);
            module_component[module_id.index()] = component_index;
        }
        std::ranges::sort(members, [&](ModuleID left, ModuleID right) noexcept {
            return module_path(semantic, left) < module_path(semantic, right);
        });
        component_members.push_back(std::move(members));
    }

    auto component_predecessors = std::vector<std::flat_set<std::size_t>>(component_members.size());
    for (auto component_index = 0uz; component_index < component_members.size();
         ++component_index) {
        for (const auto member : component_members[component_index]) {
            for (const auto dependency : complete_dependencies[member.index()]) {
                if (!module_component[dependency.index()].has_value()) {
                    invariant_violation("complete interface dependency has no component");
                }
                const auto dependency_component = *module_component[dependency.index()];
                if (dependency_component != component_index) {
                    component_predecessors[component_index].insert(dependency_component);
                }
            }
        }
    }

    auto artifacts = TargetPlanTableBuilder<TargetArtifactPlan, TargetArtifactID>(identity);
    auto component_artifacts =
        std::vector<std::optional<TargetArtifactID>>(component_members.size());
    for (auto component_index = 0uz; component_index < component_members.size();
         ++component_index) {
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

        auto forwards = std::flat_set<NominalDeclarationRef>();
        for (const auto nominal : ordered_nominals) {
            const auto owner = target_owner_module(semantic, target_declaration_ref(nominal));
            if (published_nominal(semantic, nominal)
                && module_component[owner.index()].has_value()
                && *module_component[owner.index()] == component_index) {
                forwards.insert(nominal);
            }
        }
        for (const auto member : component_members[component_index]) {
            for (const auto& [nominal, completeness] :
                 references.surface_requirements[member.index()]) {
                if (!published_nominal(semantic, nominal)) {
                    invariant_violation("published surface references a private nominal");
                }
                const auto owner = target_owner_module(semantic, target_declaration_ref(nominal));
                if (!module_component[owner.index()].has_value()) {
                    invariant_violation("interface nominal dependency has no component");
                }
                const auto owner_component = *module_component[owner.index()];
                if (owner_component == component_index || included_components[owner_component]) {
                    continue;
                }
                if (completeness == TargetTypeCompleteness::CompleteDefinition) {
                    invariant_violation("complete interface dependency was not included");
                }
                forwards.insert(nominal);
            }
        }
        auto ordered_forwards =
            std::vector<NominalDeclarationRef>(forwards.begin(), forwards.end());
        std::ranges::sort(ordered_forwards, [&](const auto left, const auto right) noexcept {
            return nominal_key(semantic, left) < nominal_key(semantic, right);
        });
        auto forward_declarations = std::vector<TargetInterfaceForwardDeclaration>();
        for (const auto nominal : ordered_forwards) {
            forward_declarations.push_back({
                .module_id = target_owner_module(semantic, target_declaration_ref(nominal)),
                .declaration = nominal,
            });
        }

        auto interface_declarations = std::vector<TargetInterfaceDeclaration>();
        for (const auto nominal : ordered_nominals) {
            const auto owner = target_owner_module(semantic, target_declaration_ref(nominal));
            if (published_nominal(semantic, nominal)
                && module_component[owner.index()].has_value()
                && *module_component[owner.index()] == component_index) {
                interface_declarations.push_back({
                    .module_id = owner,
                    .declaration = target_declaration_ref(nominal),
                });
            }
        }
        for (const auto member : component_members[component_index]) {
            for (const auto item : declarations.module_decl(member).items) {
                std::visit(
                    Overloaded {
                        [&](FunctionID id) noexcept {
                            if (declarations.function(id).visibility
                                != DeclarationVisibility::Module) {
                                interface_declarations.push_back({
                                    .module_id = member,
                                    .declaration = DeclarationRef {id},
                                });
                            }
                        },
                        [](ModuleConstantID) static noexcept {},
                        [](StructID) static noexcept {},
                        [](EnumID) static noexcept {},
                        [](TestID) static noexcept {},
                    },
                    item
                );
            }
        }

        auto predecessor_artifacts = std::vector<TargetArtifactID>();
        for (const auto predecessor : component_predecessors[component_index]) {
            if (!component_artifacts[predecessor].has_value()) {
                invariant_violation("interface graph is not scheduled dependency-first");
            }
            predecessor_artifacts.push_back(*component_artifacts[predecessor]);
        }
        const auto& anchor = declarations.module_decl(component_members[component_index].front());
        const auto& anchor_path = provenance.module_record(anchor.provenance_module).path;
        const auto id = artifacts.add(
            TargetInterfaceArtifact {
                .logical_path = interface_component_logical_path(anchor_path.components()),
                .component_members = component_members[component_index],
                .predecessor_artifacts = std::move(predecessor_artifacts),
                .forward_declarations = std::move(forward_declarations),
                .declarations = std::move(interface_declarations),
            }
        );
        component_artifacts[component_index] = id;
    }

    for (const auto module_id : module_ids) {
        auto cpp_exports = std::vector<FunctionID>();
        for (const auto item : declarations.module_decl(module_id).items) {
            const auto* function_id = std::get_if<FunctionID>(&item);
            if (function_id != nullptr
                && declarations.function(*function_id).cpp_export_origin.has_value()) {
                cpp_exports.push_back(*function_id);
            }
        }
        if (cpp_exports.empty()) {
            continue;
        }
        if (!module_component[module_id.index()].has_value()) {
            invariant_violation("C++ export module has no published interface component");
        }
        const auto& declaration = declarations.module_decl(module_id);
        const auto& path = provenance.module_record(declaration.provenance_module).path;
        static_cast<void>(artifacts.add(
            TargetCppAPIHeaderArtifact {
                .logical_path = cpp_api_header_logical_path(path.components()),
                .module_id = module_id,
                .cpp_export_declarations = std::move(cpp_exports),
                .interface_dependencies = {
                    *component_artifacts[*module_component[module_id.index()]],
                },
            }
        ));
    }

    for (const auto module_id : module_ids) {
        auto interface_dependencies = std::vector<TargetArtifactID>();
        if (module_component[module_id.index()].has_value()) {
            const auto component = *module_component[module_id.index()];
            interface_dependencies.push_back(*component_artifacts[component]);
        }
        const auto& declaration = declarations.module_decl(module_id);
        const auto& path = provenance.module_record(declaration.provenance_module).path;
        static_cast<void>(artifacts.add(
            TargetModuleImplementationArtifact {
                .logical_path = module_implementation_logical_path(path.components()),
                .schedule = std::move(*schedules[module_id.index()]),
                .interface_dependencies = std::move(interface_dependencies),
            }
        ));
    }

    auto test_runner_modules = std::vector<ModuleID>();
    if (request.test_mode != TestGenerationMode::None) {
        for (const auto test : semantic.tests().entries()) {
            const auto module_id = test.value.module_id;
            if (!std::ranges::contains(test_runner_modules, module_id)) {
                test_runner_modules.push_back(module_id);
            }
        }
        std::ranges::sort(test_runner_modules, [&](ModuleID left, ModuleID right) noexcept {
            return module_path(semantic, left) < module_path(semantic, right);
        });
        const auto runner_header = artifacts.add(
            TargetTestRunnerHeaderArtifact {
                .logical_path = "carven/generated/carven-test-runner.hpp",
                .module_runners = std::move(test_runner_modules),
            }
        );
        if (request.test_mode == TestGenerationMode::RunnerEntryPoint) {
            static_cast<void>(artifacts.add(
                TargetTestEntryArtifact {
                    .logical_path = "carven/generated/carven-test-main.cpp",
                    .runner_header_dependency = runner_header,
                }
            ));
        }
    }
    return std::move(artifacts).seal();
}

auto materialize_directives(
    const TargetPlan& plan,
    TargetArtifactID artifact_id,
    std::span<const TargetArtifactID> lowering_dependencies
) noexcept -> TargetDirectiveInputs {
    const auto& artifact = plan.artifact(artifact_id);
    auto result = TargetDirectiveInputs();
    if (artifact_source_mapping(artifact) == ArtifactSourceMappingPolicy::StableInterface) {
        result.prefix_groups.push_back({
            .directives = {TargetDirective {.bytes = "#pragma once"}},
            .attribution = TargetCompilerOwnedAttribution {
                .reason = TargetCompilerReason::ArtifactScaffolding,
            },
        });
    }
    auto dependencies = std::flat_set<TargetArtifactID>();
    for (const auto dependency : artifact_dependencies(artifact)) {
        dependencies.insert(dependency);
    }
    for (const auto dependency : lowering_dependencies) {
        if (dependency.owner() != plan.identity() || dependency.index() >= artifact_id.index()) {
            invariant_violation("lowering dependency is foreign or not dependency-first");
        }
        static_cast<void>(plan.artifact(dependency));
        dependencies.insert(dependency);
    }
    for (const auto dependency : dependencies) {
        result.suffix_groups.push_back({
            .directives = {TargetDirective {
                .bytes =
                    std::format("#include <{}>", artifact_logical_path(plan.artifact(dependency))),
            }},
            .attribution = TargetCompilerOwnedAttribution {
                .reason = TargetCompilerReason::ArtifactScaffolding,
            },
        });
    }
    return result;
}
