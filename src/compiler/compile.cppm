module carven:compiler.compile;

import :artifacts;
import :backend.generation.request;
import :compiler.request;
import :diagnostics.diagnosed;
import :source.manager;
import std;

auto compile(
    const SourceManager& sources,
    CompilationRequest compilation,
    const TargetPlanningRequest& generation
) noexcept -> std::expected<Diagnosed<GeneratedArtifactSet>, Diagnostics>;
