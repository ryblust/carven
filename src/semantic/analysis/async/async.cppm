module carven:semantic.analysis.async;

import :semantic.analysis.diagnostics;
import :semantic.semir.async;
import :semantic.semir.program;
import std;

auto analyze_async_contracts(const SemIRProgram& program, AnalysisDiagnostics diagnostics) noexcept
    -> AnalysisResult<void>;

auto analyze_async_cancellation(const SemIRProgram& program) noexcept -> AsyncCancellationFacts;
