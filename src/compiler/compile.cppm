module carven:compiler.compile;

import :artifacts;
import :backend.generation.request;
import :compilation.request;
import :diagnostics.diagnosed;
import :source.manager;
import std;

auto compile(
    const SourceManager& sources,
    CompilationRequest compilation,
    TargetGenerationRequest generation
) noexcept -> std::expected<Diagnosed<ArtifactSet>, Diagnostics>;
