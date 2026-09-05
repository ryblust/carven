module carven:backend.generate.impl;

import :backend.emission.render;
import :backend.emit;
import :backend.generate;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import std;

auto generate_artifacts(SemIRProgram semantic, const TargetPlanningRequest& request) noexcept
    -> GeneratedArtifactSet {
    const auto compilation = PlannedCompilation::build(std::move(semantic), request);
    auto artifacts = std::vector<GeneratedArtifact>();
    artifacts.reserve(compilation.target().artifact_count());
    for (const auto entry : compilation.target().artifacts()) {
        const auto artifact_id = entry.id;
        const auto& artifact = entry.value;
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
