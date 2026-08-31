module carven:backend.generate.impl;

import :backend.emit;
import :backend.generate;
import :backend.generation.program;
import :backend.lower;
import std;

auto generate_target(SemanticProgram semantic, TargetGenerationRequest request) noexcept
    -> ArtifactSet {
    const auto program = TargetProgram::build(std::move(semantic), std::move(request));
    auto artifacts = std::vector<GeneratedArtifact>();
    artifacts.reserve(program.artifacts().size());
    for (auto index = 0uz; index < program.artifacts().size(); ++index) {
        const auto artifact_id = TargetArtifactID::from_index(static_cast<std::uint32_t>(index));
        artifacts.push_back(emit(lower_target_unit(program, artifact_id)));
    }
    return ArtifactSet(std::move(artifacts));
}
