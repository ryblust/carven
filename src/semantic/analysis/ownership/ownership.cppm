module carven:semantic.analysis.ownership;

import :semantic.analysis.diagnostics;
import :semantic.semir.program;
import :semantic.semir.structured;
import std;

auto analyze_body_batch(std::span<const SemIRBody> bodies, ProgramDraft& draft) noexcept
    -> AnalysisResult<void>;
