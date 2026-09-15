module carven:compiler.analysis;

import :compiler.request;
import :diagnostics.diagnosed;
import :semantic.evaluation.output;
import :semantic.semir.program;
import :source.manager;
import std;

// The program owns its source snapshots; diagnostic locations use the supplied sources.
auto analyze_compilation(
    const SourceManager& sources,
    CompilationRequest request,
    const ExecutionOutput& output = {}
) noexcept -> std::expected<Diagnosed<SemIRProgram>, Diagnostics>;
