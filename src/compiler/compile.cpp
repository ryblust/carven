module carven:compiler.compile.impl;

import :artifacts;
import :backend.generate;
import :backend.generation.request;
import :compiler.analysis;
import :compiler.compile;
import :diagnostics.builder;
import :diagnostics.code;
import :semantic.analysis.program;
import :semantic.evaluation.execution;
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
    TimingOutput timings,
    AnalysisNativeCall native_call
) noexcept -> std::expected<Diagnosed<GeneratedArtifactSet>, Diagnostics> {
    const auto semantic = analyze_compilation(
        sources.snapshot(),
        batch,
        {.output = output,
         .timings = timings,
         .collect_source_observations = false,
         .capture_output = false,
         .host_effects = HostEffects::Allow,
         .completion = std::nullopt,
         .source_query = false,
         .excluded = {},
         .native_call = native_call}
    );
    auto diagnostics = Diagnostics(semantic.diagnostics().begin(), semantic.diagnostics().end());
    if (semantic.program() == nullptr) {
        return std::unexpected(std::move(diagnostics));
    }
    auto artifacts = generate_artifacts(*semantic.program(), generation, std::nullopt, timings);
    if (!artifacts) {
        diagnostics.push_back(
            DiagnosticBuilder(DiagnosticCode::CompilationInput, std::move(artifacts.error()))
                .build()
        );
        return std::unexpected(std::move(diagnostics));
    }

    return Diagnosed<GeneratedArtifactSet> {
        .value = std::move(*artifacts),
        .diagnostics = std::move(diagnostics),
    };
}
