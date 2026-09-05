module carven:semantic.analysis.pipeline.publish.impl;

import :semantic.analysis.body.resolve;
import :semantic.analysis.ownership;
import :semantic.analysis.pipeline.publish;
import :semantic.analysis.validation;
import :semantic.semir.structured;
import std;

auto publish_semantic_program(ProgramDraft&& input) noexcept -> AnalysisResult<SemIRProgram> {
    auto draft = std::move(input);
    auto solved = draft.solve_construction();
    if (!solved.has_value()) {
        return std::unexpected(solved.error());
    }
    auto global = validate_global_semantic_contracts(draft);
    if (!global.has_value()) {
        return std::unexpected(global.error());
    }
    auto drafts = draft.take_body_drafts();
    auto bodies = std::vector<SemIRBody>();
    bodies.reserve(drafts.size());
    for (auto& body : drafts) {
        bodies.push_back(resolve_body(std::move(body), draft));
    }
    auto analyzed = analyze_body_batch(bodies, draft);
    if (!analyzed.has_value()) {
        return std::unexpected(analyzed.error());
    }
    draft.publish_bodies(std::move(bodies));
    return std::move(draft).seal();
}
