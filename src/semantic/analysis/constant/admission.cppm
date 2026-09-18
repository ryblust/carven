module carven:semantic.analysis.constant.admission;

import :semantic.analysis.diagnostics;
import :semantic.analysis.program;
import :semantic.semir.ids;
import :semantic.semir.structured;

// Validate the complete source operation tree, independently of calls or execution.
auto validate_constant_function(
    ProgramDraft& draft,
    FunctionID function,
    const StructuredBodyDraft& body
) noexcept -> AnalysisResult<void>;

auto validate_constant_body(ProgramDraft& draft, const StructuredBodyDraft& body) noexcept
    -> AnalysisResult<void>;
