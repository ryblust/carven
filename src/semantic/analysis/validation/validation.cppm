module carven:semantic.analysis.validation;

import :semantic.analysis.diagnostics;
import :semantic.analysis.program;
import :semantic.analysis.types.contents;
import :semantic.semir.program;
import :semantic.semir.structured;
import std;

auto verify_semantic_body(
    const SemIRBody& body,
    ProgramDraft& draft,
    const BodyStore& bodies
) noexcept -> void;

auto validate_global_semantic_contracts(
    ProgramDraft& draft,
    std::span<const TypeContents> types
) noexcept -> AnalysisResult<void>;
