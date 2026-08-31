module carven:semantic.analysis.control.internal;

import :semantic.analysis.session;
import :semantic.analysis.session.read;
import :semantic.analysis.control;
import std;

struct RecordedControlSummaries final {
    std::vector<ControlSummary> expressions;
    std::vector<ControlSummary> statements;
    std::vector<ControlSummary> blocks;
    std::vector<std::vector<CatchControlSummary>> catches;
    std::vector<std::vector<HIRTypeID>> unhandled;
};

auto evaluate_callable_control(
    SemanticDraftView hir,
    const std::vector<std::vector<HIRTypeID>>& failure_sets,
    CallableID callable,
    std::vector<std::vector<std::uint32_t>>* dependencies
) noexcept -> ControlSummary;

auto record_control(
    SemanticDraftView hir,
    const std::vector<std::vector<HIRTypeID>>& failure_sets
) noexcept -> RecordedControlSummaries;

auto solve_failure_contracts(SemanticDraftView builder) noexcept
    -> std::vector<std::vector<HIRTypeID>>;

auto derive_evaluation_effects(SemanticDraftView hir) noexcept -> std::vector<EvaluationEffect>;
