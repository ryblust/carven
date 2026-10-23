module carven:compiler.compile;

import :artifacts;
import :backend.generation.request;
import :diagnostics.diagnosed;
import :semantic.analysis.program;
import :semantic.evaluation.output;
import :source.batch;
import :source.manager;
import :support.timing;
import std;

auto compile(
    const SourceManager& sources,
    SourceBatch batch,
    const TargetPlanningRequest& generation,
    ExecutionOutput output = {},
    TimingOutput timings = {},
    AnalysisNativeCall native_call = {}
) noexcept -> std::expected<Diagnosed<GeneratedArtifactSet>, Diagnostics>;
