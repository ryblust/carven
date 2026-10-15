module carven:backend.generate.impl;

import :backend.emission.emit;
import :backend.emission.render;
import :backend.generate;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
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
    auto modules = std::flat_set<ModuleID>();
    for (const auto module : semantic.declarations().modules()) {
        const auto& path = semantic.provenance().module_record(module.value.provenance_module).path;
        if (paths.contains(path)) {
            modules.insert(module.id);
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
        return !selected_modules || std::ranges::contains(*selected_modules, path);
    };
    auto artifacts = std::vector<GeneratedArtifact>();
    artifacts.reserve(compilation.target().artifact_count());
    for (const auto entry : compilation.target().artifacts()) {
        const auto artifact_id = entry.id;
        const auto& artifact = entry.value;
        if (request.library && !request.library->emit_implementations) {
            if (const auto* implementation =
                    std::get_if<TargetModuleImplementationArtifact>(&artifact)) {
                if (library->contains(implementation->schedule.module_id)) {
                    continue;
                }
            }
        }
        if (selected_modules
            && !artifact.visit(
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
                    [](const TargetTestEntryArtifact&) static noexcept { return true; }
                }
            )) {
            continue;
        }
        auto logical_path = std::string(artifact_logical_path(artifact));
        const auto policy =
            artifact_source_mapping(artifact) == ArtifactSourceMappingPolicy::StableInterface
            ? EmissionPolicy {StableInterfaceEmission {}}
            : EmissionPolicy {SourceAttributedEmission {.generated_origin = logical_path}};
        auto unit = lower_artifact(compilation, artifact_id);
        auto emitted = emit(std::move(unit), logical_path, artifact_role(artifact), policy);
        if (const auto* implementation =
                std::get_if<TargetModuleImplementationArtifact>(&artifact)) {
            const auto& module =
                semantic.declarations().module_decl(implementation->schedule.module_id);
            const auto& provenance_module =
                semantic.provenance().module_record(module.provenance_module);
            emitted.source_path =
                semantic.provenance().source_snapshot(provenance_module.source_id).display_origin();
        }
        artifacts.push_back(std::move(emitted));
    }
    return GeneratedArtifactSet(std::move(artifacts));
}
