module carven:semantic.analysis.ownership;

import :semantic.analysis.diagnostics;
import :semantic.analysis.program;
import :semantic.analysis.types.contents;
import :semantic.semir.program;
import :semantic.semir.structured;
import std;

auto analyze_body_batch(
    const BodyStore& bodies,
    ProgramDraft& draft,
    std::span<const TypeContents> types
) noexcept -> AnalysisResult<void>;
