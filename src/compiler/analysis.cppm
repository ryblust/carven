module carven:compiler.analysis;

import :diagnostics.diagnosed;
import :semantic.evaluation.output;
import :semantic.semir.program;
import :source.batch;
import :source.manager;
import std;

// The program owns its source snapshots; diagnostic locations use the supplied sources.
auto analyze_compilation(
    const SourceManager& sources,
    SourceBatch batch,
    const ExecutionOutput& output = {}
) noexcept -> std::expected<Diagnosed<SemIRProgram>, Diagnostics>;
