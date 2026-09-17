module carven:compiler.compile;

import :artifacts;
import :backend.generation.request;
import :diagnostics.diagnosed;
import :semantic.evaluation.output;
import :source.batch;
import :source.manager;
import std;

auto compile(
    const SourceManager& sources,
    SourceBatch batch,
    const TargetPlanningRequest& generation,
    const ExecutionOutput& output = {}
) noexcept -> std::expected<Diagnosed<GeneratedArtifactSet>, Diagnostics>;
