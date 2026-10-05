module carven:semantic.analyze;

import :diagnostics.diagnosed;
import :diagnostics.diagnostic;
import :frontend.program;
import :semantic.analysis.source;
import :semantic.evaluation.output;
import :semantic.semir.program;
import :support.timing;
import std;

auto analyze(
    SyntaxProgram syntax,
    const ExecutionOutput& output = {},
    const TimingOutput& timings = {},
    const SourceAnalysisOutput& source_output = {}
) noexcept -> std::expected<Diagnosed<SemIRProgram>, Diagnostics>;
