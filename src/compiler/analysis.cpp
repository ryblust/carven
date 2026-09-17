module carven:compiler.analysis.impl;

import :compiler.analysis;
import :diagnostics.diagnosed;
import :frontend.program.parse;
import :semantic.analyze;
import :semantic.evaluation.output;
import :semantic.semir.program;
import :source.batch;
import :source.manager;
import std;

auto analyze_compilation(
    const SourceManager& sources,
    SourceBatch batch,
    const ExecutionOutput& output
) noexcept -> std::expected<Diagnosed<SemIRProgram>, Diagnostics> {
    auto syntax = parse_program(sources, batch);
    if (!syntax.has_value()) {
        return std::unexpected(std::move(syntax.error()));
    }
    return analyze(std::move(*syntax), output);
}
