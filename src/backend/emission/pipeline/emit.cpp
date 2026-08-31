module carven:backend.emit.impl;

import :artifacts;
import :backend.emission.layout;
import :backend.emission.render;
import :backend.emit;
import :backend.target;
import :backend.target.unit;
import std;

auto emit(TargetUnit unit) noexcept -> GeneratedArtifact {
    auto logical_path = unit.root().logical_path;
    auto content = render_layout(TargetRenderer(unit).render_unit(), 100uz);
    return GeneratedArtifact {
        .logical_path = std::move(logical_path),
        .role = unit.root().role,
        .source_mapping = unit.root().source_mapping,
        .content = std::move(content),
    };
}
