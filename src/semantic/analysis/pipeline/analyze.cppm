module carven:semantic.analyze;

import :diagnostics.diagnosed;
import :diagnostics.diagnostic;
import :frontend.program;
import :semantic.analysis.source;
import :semantic.evaluation.execution;
import :semantic.evaluation.output;
import :semantic.semir.program;
import :support.timing;
import std;

auto analyze(
    SyntaxProgram syntax,
    ExecutionOutput output = {},
    TimingOutput timings = {},
    SourceAnalysisOutput source_output = {},
    HostEffects host_effects = HostEffects::Deny
) noexcept -> std::expected<Diagnosed<SemIRProgram>, Diagnostics>;
