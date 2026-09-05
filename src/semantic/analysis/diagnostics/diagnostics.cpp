module carven:semantic.analysis.diagnostics.impl;

import :semantic.analysis.diagnostics;
import std;

AnalysisDiagnostics::AnalysisDiagnostics(DiagnosticSink& sink) noexcept
    : diagnostic_sink(std::addressof(sink)) {}

auto AnalysisDiagnostics::error(Diagnostic diagnostic) const noexcept -> AnalysisFailure {
    diagnostic_sink->emit(std::move(diagnostic));
    return AnalysisFailure();
}

auto AnalysisDiagnostics::warning(Diagnostic diagnostic) const noexcept -> void {
    diagnostic_sink->emit(std::move(diagnostic));
}

auto AnalysisDiagnostics::has_errors() const noexcept -> bool {
    return diagnostic_sink->has_errors();
}
