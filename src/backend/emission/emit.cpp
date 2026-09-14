module carven:backend.emission.emit.impl;

import :artifacts;
import :backend.emission.emit;
import :backend.emission.layout;
import :backend.emission.render;
import :backend.target.unit;
import :backend.target;
import std;

auto emit(
    TargetUnit unit,
    std::string logical_path,
    GeneratedArtifactRole role,
    EmissionPolicy policy
) noexcept -> GeneratedArtifact {
    const auto source_mapping = std::holds_alternative<StableInterfaceEmission>(policy)
        ? ArtifactSourceMappingPolicy::StableInterface
        : ArtifactSourceMappingPolicy::SourceAttributed;
    auto content = render_layout(TargetRenderer(unit, policy).render_unit(), 80uz);
    return GeneratedArtifact {
        .logical_path = std::move(logical_path),
        .role = role,
        .source_mapping = source_mapping,
        .content = std::move(content),
    };
}
