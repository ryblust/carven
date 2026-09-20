module carven:backend.generate.impl;

import :backend.emission.emit;
import :backend.emission.render;
import :backend.generate;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import :semantic.semir.decl;
import :source.provenance;
import :support.visit;
import std;

auto generate_artifacts(
    SemIRProgram semantic,
    const TargetPlanningRequest& request,
    std::optional<std::span<const CanonicalModulePath>> selected_modules
) noexcept -> GeneratedArtifactSet {
    const auto compilation = PlannedCompilation::build(std::move(semantic), request);
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
        if (selected_modules
            && !artifact.visit(
                Overloaded {
                    [&](const TargetInterfaceArtifact& value) noexcept {
                        return std::ranges::any_of(value.component_members, selected);
                    },
                    [&](const TargetCppAPIHeaderArtifact& value) noexcept {
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
        artifacts.push_back(emit(std::move(unit), logical_path, artifact_role(artifact), policy));
    }
    return GeneratedArtifactSet(std::move(artifacts));
}
