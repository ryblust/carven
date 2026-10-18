module carven:semantic.analysis.program.publication.impl;

import :semantic.analysis.async;
import :semantic.analysis.model;
import :semantic.analysis.nullability;
import :semantic.analysis.ownership;
import :semantic.analysis.program;
import :semantic.analysis.validation;
import :semantic.semir.passing;
import :support.timing;
import std;

auto ProgramDraft::finish() && noexcept -> AnalysisResult<SemIRProgram> {
    const auto diagnostics = analysis_diagnostics;
    const auto timings = analysis_timings;
    auto solve_scope = TimingScope(timings, TimingStage::SemanticSolving);
    auto solved = [this]() noexcept {
        auto draft = std::move(*this);
        return std::move(draft).resolve();
    }();
    solve_scope.stop();
    if (!solved) {
        return std::unexpected(solved.error());
    }
    const auto generics = std::move(solved->generics);
    auto result = AnalysisResult<SemIRProgram> {std::move(solved->program)};
    if (const auto failure = diagnostics.failure()) {
        return std::unexpected(*failure);
    }
    const auto validation_scope = TimingScope(timings, TimingStage::SemanticValidation);
    const auto& program = *result;
    for (const auto entry : program.bodies().entries()) {
        verify_semantic_body(entry.value, program);
    }
    auto checked = validate_global_semantic_contracts(program, diagnostics);
    if (!checked) {
        return std::unexpected(checked.error());
    }
    {
        const auto model = AnalysisModelView(program, generics);
        checked = analyze_async_contracts(model, diagnostics);
        if (!checked) {
            return std::unexpected(checked.error());
        }
        auto passing = ReadPassingFacts {};
        checked = analyze_body_batch(model, diagnostics, &passing);
        if (!checked) {
            return std::unexpected(checked.error());
        }
        result->publish_read_passing_facts(std::move(passing));
        checked = check_pointer_nullability(model, diagnostics);
        if (!checked) {
            return std::unexpected(checked.error());
        }
    }
    result->publish_surfaces();
    result->publish_bodies();
    result->cancellation_facts =
        std::make_unique<const AsyncCancellationFacts>(analyze_async_cancellation(*result));
    return result;
}
