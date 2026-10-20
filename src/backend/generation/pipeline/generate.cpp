module carven:backend.generate.impl;

import :backend.emission.emit;
import :backend.emission.render;
import :backend.generate;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import :backend.target;
import :semantic.semir.decl;
import :semantic.semir.generic;
import :source.provenance;
import :support.timing;
import :support.visit;
import std;

namespace {

auto library_modules(const SemIRProgram& semantic, const TargetPlanningRequest& request) noexcept
    -> std::expected<std::flat_set<ModuleID>, std::string> {
    if (!request.library) {
        return {};
    }
    const auto& library = *request.library;
    auto paths = std::flat_set<CanonicalModulePath>();
    for (const auto& path : library.modules) {
        if (!paths.insert(path).second) {
            return std::unexpected(std::format("duplicate library module '{}'", path.value()));
        }
    }
    auto owned = std::flat_set<CanonicalModulePath>();
    for (const auto& path : library.owned_modules) {
        if (!owned.insert(path).second) {
            return std::unexpected(
                std::format("duplicate owned library module '{}'", path.value())
            );
        }
        if (!paths.contains(path)) {
            return std::unexpected(
                std::format("owned module '{}' is not in the library", path.value())
            );
        }
    }
    auto modules = std::flat_set<ModuleID>();
    for (const auto module_decl : semantic.declarations().modules()) {
        const auto& path =
            semantic.provenance().module_record(module_decl.value.provenance_module).path;
        if (paths.contains(path)) {
            modules.insert(module_decl.id);
            paths.erase(path);
        }
    }
    if (!paths.empty()) {
        return std::unexpected(std::format("unknown library module '{}'", paths.begin()->value()));
    }
    for (const auto function : semantic.declarations().functions()) {
        if (modules.contains(function.value.module_id) && function.value.entry_point) {
            return std::unexpected("library modules cannot declare a program entry point");
        }
    }
    return modules;
}

} // namespace

auto generate_artifacts(
    const SemIRProgram& semantic,
    const TargetPlanningRequest& request,
    std::optional<std::span<const CanonicalModulePath>> selected_modules,
    TimingOutput timings
) noexcept -> std::expected<GeneratedArtifactSet, std::string> {
    const auto scope = TimingScope(timings, TimingStage::CppGeneration);
    const auto library = library_modules(semantic, request);
    if (!library) {
        return std::unexpected(library.error());
    }
    const auto compilation = PlannedCompilation::build(semantic, request, *library);
    for (const auto entry : compilation.target().artifacts()) {
        if (const auto* interface = std::get_if<TargetInterfaceArtifact>(&entry.value)) {
            const auto count =
                std::ranges::count_if(interface->component_members, [&](ModuleID id) noexcept {
                    return library->contains(id);
                });
            if (count != 0
                && static_cast<std::size_t>(count) != interface->component_members.size()) {
                return std::unexpected(
                    "library modules share an interface dependency cycle with application modules"
                );
            }
        }
    }
    const auto selected = [&](ModuleID id) noexcept {
        const auto& program = compilation.semantic();
        const auto& module_record = program.declarations().module_decl(id);
        const auto& path = program.provenance().module_record(module_record.provenance_module).path;
        if (selected_modules) {
            return std::ranges::contains(*selected_modules, path);
        }
        return !library->contains(id)
            || std::ranges::contains(request.library->owned_modules, path);
    };
    auto visited = std::vector<bool>(compilation.target().artifact_count());
    auto generated =
        std::vector<std::optional<GeneratedArtifact>>(compilation.target().artifact_count());
    auto includes =
        std::vector<std::vector<TargetArtifactID>>(compilation.target().artifact_count());
    auto pending = std::vector<TargetArtifactID>();
    const auto request_artifact = [&](TargetArtifactID id) noexcept {
        if (!visited[id.index()]) {
            visited[id.index()] = true;
            pending.push_back(id);
        }
    };
    for (const auto entry : compilation.target().artifacts()) {
        const auto root = entry.value.visit(
            Overloaded {
                [&](const TargetInterfaceArtifact& value) noexcept {
                    return std::ranges::any_of(value.component_members, selected);
                },
                [&](const TargetCppAPIHeaderArtifact& value) noexcept {
                    return selected(value.module_id);
                },
                [&](const TargetCppProviderHeaderArtifact& value) noexcept {
                    return selected(value.module_id);
                },
                [&](const TargetModuleImplementationArtifact& value) noexcept {
                    return selected(value.schedule.module_id);
                },
                [](const TargetTestRunnerHeaderArtifact&) static noexcept { return true; },
                [](const TargetTestEntryArtifact&) static noexcept { return true; },
            }
        );
        if (root) {
            request_artifact(entry.id);
        }
    }
    for (auto next = 0uz; next != pending.size(); ++next) {
        const auto artifact_id = pending[next];
        const auto& artifact = compilation.target().artifact(artifact_id);
        auto logical_path = std::string(artifact_logical_path(artifact));
        const auto policy =
            artifact_source_mapping(artifact) == ArtifactSourceMappingPolicy::StableInterface
            ? EmissionPolicy {StableInterfaceEmission {}}
            : EmissionPolicy {SourceAttributedEmission {.generated_origin = logical_path}};
        auto unit = lower_artifact(compilation, artifact_id);
        // Partial stdout output keeps the selected modules. Directory output
        // includes the generated headers required by their finished units.
        if (!selected_modules) {
            auto& dependencies = includes[artifact_id.index()];
            const auto generated_dependencies = unit.generated_dependencies();
            dependencies.assign(generated_dependencies.begin(), generated_dependencies.end());
            for (const auto dependency : dependencies) {
                request_artifact(dependency);
            }
        }
        auto emitted = emit(std::move(unit), logical_path, artifact_role(artifact), policy);
        if (const auto* implementation =
                std::get_if<TargetModuleImplementationArtifact>(&artifact)) {
            const auto& module_decl =
                semantic.declarations().module_decl(implementation->schedule.module_id);
            const auto& provenance_module =
                semantic.provenance().module_record(module_decl.provenance_module);
            emitted.source_path =
                semantic.provenance().source_snapshot(provenance_module.source_id).display_origin();
        }
        generated[artifact_id.index()] = std::move(emitted);
    }
    // A translation unit also references the implementations named by its
    // included headers. Publish that closure with the same link requirements.
    for (const auto id : pending) {
        auto& artifact = *generated[id.index()];
        auto dependencies = std::flat_set<std::string>(
            artifact.link_dependencies.begin(),
            artifact.link_dependencies.end()
        );
        auto inspected = std::flat_set<TargetArtifactID> {id};
        auto headers = includes[id.index()];
        for (auto next = 0uz; next != headers.size(); ++next) {
            const auto header = headers[next];
            if (!inspected.insert(header).second) {
                continue;
            }
            const auto& inherited = generated[header.index()]->link_dependencies;
            dependencies.insert_range(inherited);
            headers.append_range(includes[header.index()]);
        }
        dependencies.erase(artifact.logical_path);
        artifact.link_dependencies.assign(dependencies.begin(), dependencies.end());
    }
    auto artifacts = std::vector<GeneratedArtifact>();
    for (auto& artifact : generated) {
        if (artifact) {
            artifacts.push_back(std::move(*artifact));
        }
    }
    return GeneratedArtifactSet(std::move(artifacts));
}
