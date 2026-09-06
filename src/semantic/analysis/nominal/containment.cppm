module carven:semantic.analysis.nominal.containment;

import :semantic.analysis.diagnostics;
import :semantic.analysis.program;
import :semantic.semir.program;

auto analyze_nominal_containment(ProgramDraft& draft) noexcept -> AnalysisResult<void>;
