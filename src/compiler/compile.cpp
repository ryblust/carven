module carven:compiler.compile.impl;

import :artifacts;
import :backend.generate;
import :backend.generation.request;
import :compiler.analysis;
import :compiler.compile;
import :semantic.evaluation.output;
import :source.batch;
import :source.manager;
import std;

auto compile(
    const SourceManager& sources,
    SourceBatch batch,
    const TargetPlanningRequest& generation,
    const ExecutionOutput& output
) noexcept -> std::expected<Diagnosed<GeneratedArtifactSet>, Diagnostics> {
    auto semantic = analyze_compilation(sources, batch, output);
    if (!semantic.has_value()) {
        return std::unexpected(std::move(semantic.error()));
    }

    auto artifacts = generate_artifacts(std::move(semantic->value), generation);

    return Diagnosed<GeneratedArtifactSet> {
        .value = std::move(artifacts),
        .diagnostics = std::move(semantic->diagnostics),
    };
}
