module carven:compiler.compile.impl;

import :artifacts;
import :backend.generate;
import :backend.generation.request;
import :compiler.analysis;
import :compiler.compile;
import :diagnostics.builder;
import :diagnostics.code;
import :semantic.evaluation.output;
import :source.batch;
import :source.manager;
import :support.timing;
import std;

auto compile(
    const SourceManager& sources,
    SourceBatch batch,
    const TargetPlanningRequest& generation,
    ExecutionOutput output,
    TimingOutput timings
) noexcept -> std::expected<Diagnosed<GeneratedArtifactSet>, Diagnostics> {
    auto semantic = analyze_compilation(sources, batch, output, timings);
    if (!semantic.has_value()) {
        return std::unexpected(std::move(semantic.error()));
    }
    auto artifacts =
        generate_artifacts(std::move(semantic->value), generation, std::nullopt, timings);
    if (!artifacts) {
        semantic->diagnostics.push_back(
            DiagnosticBuilder(DiagnosticCode::CompilationInput, std::move(artifacts.error()))
                .build()
        );
        return std::unexpected(std::move(semantic->diagnostics));
    }

    return Diagnosed<GeneratedArtifactSet> {
        .value = std::move(*artifacts),
        .diagnostics = std::move(semantic->diagnostics),
    };
}
