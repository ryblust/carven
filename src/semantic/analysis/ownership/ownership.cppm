module carven:semantic.analysis.ownership;

import :semantic.analysis.diagnostics;
import :semantic.analysis.model;
import :semantic.semir.passing;
import std;

auto analyze_body_batch(
    const AnalysisModelView& model,
    AnalysisDiagnostics diagnostics,
    ReadPassingFacts* passing
) noexcept -> AnalysisResult<void>;
