module carven:semantic.analysis.validation;

import :semantic.analysis.diagnostics;
import :semantic.semir.program;
import :semantic.semir.structured;
import std;

auto verify_semantic_body(
    const SemIRBody& body,
    ProgramDraft& draft,
    std::span<const SemIRBody> bodies
) noexcept -> void;

auto validate_global_semantic_contracts(ProgramDraft& draft) noexcept -> AnalysisResult<void>;
