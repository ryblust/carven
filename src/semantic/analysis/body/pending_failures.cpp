module carven:semantic.analysis.body.pending_failures.impl;

import :diagnostics.code;
import :semantic.analysis.body.context;
import :semantic.analysis.failure;
import std;

namespace body_elaboration {

auto BodyElaborator::failure_context_for_current_path() const noexcept -> const FailureContext& {
    return reachable && reference_path_reachable ? failure_contexts.back() : dead_failure_context;
}

auto BodyElaborator::route_pending(
    PendingFailureTerms terms,
    const FailureContext& target,
    std::optional<Span> propagation_span
) noexcept -> void {
    const auto combined = draft().add_union_failure_term(std::move(terms));
    if (propagation_span.has_value()) {
        draft().require_non_empty_failures(combined, origin(*propagation_span));
    }
    draft().add_failure_contribution(target.term, combined);
}

auto BodyElaborator::discard_pending(BuiltExpression& expression) noexcept -> void {
    expression.pending_failures.clear();
}

auto BodyElaborator::collect_pending(
    PendingFailureTerms& destination,
    BuiltExpression& expression
) noexcept -> void {
    append_pending(destination, take_pending(expression));
}

auto BodyElaborator::consume_pending(BuiltExpression& expression, Span span) noexcept
    -> AnalysisResult<void> {
    if (!expression.pending_failures.empty()) {
        draft().require_empty_failures(
            draft().add_union_failure_term(take_pending(expression)),
            origin(span),
            EmptyFailureRequirementKind::OrdinaryConsumption
        );
    } else {
        discard_pending(expression);
    }
    return {};
}

auto BodyElaborator::propagate_pending(BuiltExpression& expression, Span span) noexcept
    -> AnalysisResult<void> {
    if (expression.pending_failures.empty()) {
        return std::unexpected(fail(
            span,
            DiagnosticCode::EffectPropagateRedundant,
            "postfix '?' requires a fallible expression"
        ));
    }
    route_pending(take_pending(expression), failure_context_for_current_path(), span);
    return {};
}

} // namespace body_elaboration
