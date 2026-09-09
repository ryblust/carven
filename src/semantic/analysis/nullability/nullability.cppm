module carven:semantic.analysis.nullability;

import :semantic.analysis.diagnostics;
import :semantic.semir.program;

auto check_pointer_nullability(
    const SemIRProgram& program,
    AnalysisDiagnostics diagnostics
) noexcept -> AnalysisResult<void>;
