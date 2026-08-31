module carven:compiler.compile.impl;

import :artifacts;
import :backend.generate;
import :compilation.request;
import :compiler.compile;
import :frontend.program.parse;
import :semantic.analyze;
import :source.manager;
import std;

auto compile(
    const SourceManager& sources,
    CompilationRequest compilation,
    TargetGenerationRequest generation
) noexcept -> std::expected<Diagnosed<ArtifactSet>, Diagnostics> {
    auto syntax = parse(sources, compilation.inputs);
    if (!syntax.has_value()) {
        return std::unexpected(std::move(syntax.error()));
    }

    auto semantic = analyze(std::move(*syntax));
    if (!semantic.has_value()) {
        return std::unexpected(std::move(semantic.error()));
    }

    auto artifacts = generate_target(std::move(semantic->value), std::move(generation));

    return Diagnosed<ArtifactSet> {
        .value = std::move(artifacts),
        .diagnostics = std::move(semantic->diagnostics),
    };
}
