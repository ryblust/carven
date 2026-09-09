module carven:semantic.analysis.program.publication.impl;

import :semantic.analysis.ownership;
import :semantic.analysis.program;
import :semantic.analysis.types.contents;
import :semantic.analysis.validation;
import :semantic.semir.publication;
import std;

auto ProgramDraft::finish() && noexcept -> AnalysisResult<SemIRProgram> {
    const auto diagnostics = analysis_diagnostics;
    auto result = [this]() noexcept {
        auto draft = std::move(*this);
        return std::move(draft).resolve();
    }();
    if (!result) {
        return result;
    }
    if (const auto failure = diagnostics.failure()) {
        return std::unexpected(*failure);
    }
    const auto& program = *result;
    validate_semantic_storage(program);
    for (const auto entry : program.bodies().entries()) {
        verify_semantic_body(entry.value, program);
    }
    const auto types = compute_type_contents(program.types(), program.declarations());
    auto checked = validate_global_semantic_contracts(program, diagnostics, types);
    if (!checked) {
        return std::unexpected(checked.error());
    }
    checked = analyze_body_batch(program, diagnostics, types);
    if (!checked) {
        return std::unexpected(checked.error());
    }
    return result;
}
