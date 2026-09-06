module carven:semantic.analysis.pipeline.publish.impl;

import :semantic.analysis.ownership;
import :semantic.analysis.pipeline.publish;
import :semantic.analysis.program;
import :semantic.analysis.types.contents;
import :semantic.analysis.validation;
import :semantic.semir.structured;
import std;

auto publish_semantic_program(ProgramDraft&& input) noexcept -> AnalysisResult<SemIRProgram> {
    auto draft = std::move(input);
    const auto types = compute_type_contents(draft.types(), draft.declarations());
    auto global = validate_global_semantic_contracts(draft, types);
    if (!global.has_value()) {
        return std::unexpected(global.error());
    }
    auto analyzed = analyze_body_batch(draft.bodies(), draft, types);
    if (!analyzed.has_value()) {
        return std::unexpected(analyzed.error());
    }
    return std::move(draft).seal();
}
