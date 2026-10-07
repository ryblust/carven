module carven:semantic.analysis.nullability;

import :semantic.analysis.diagnostics;
import :semantic.analysis.model;

auto check_pointer_nullability(
    const AnalysisModelView& analysis,
    AnalysisDiagnostics diagnostics
) noexcept -> AnalysisResult<void>;
