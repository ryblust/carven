module carven:backend.emit.impl;

import :artifacts;
import :backend.emission.layout;
import :backend.emission.render;
import :backend.emit;
import :backend.target;
import :backend.target.unit;
import std;

namespace {

auto artifact_path(const TargetUnit& unit) noexcept -> std::string_view {
    return std::visit(
        [](const auto& value) static noexcept -> std::string_view { return value.logical_path; },
        unit.root()
    );
}

} // namespace

auto emit(TargetUnit unit) noexcept -> GeneratedArtifact {
    auto logical_path = std::string(artifact_path(unit));
    auto content =
        render_layout(TargetRenderer(unit, logical_path).render_unit(unit.root()), 100uz);
    return GeneratedArtifact {
        .logical_path = std::move(logical_path),
        .content = std::move(content),
    };
}
