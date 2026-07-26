module carven:semantic.analysis.control.internal;

import :semantic.analysis.builder;
import :semantic.analysis.control;
import std;

struct RecordedControl final {
    std::vector<ControlSummary> expressions;
    std::vector<ControlSummary> statements;
    std::vector<ControlSummary> blocks;
    std::vector<std::vector<std::vector<HIRTypeID>>> catches;
    std::vector<std::vector<HIRTypeID>> unhandled;
};

auto evaluate_callable_control(
    const SemanticConstruction& hir,
    const std::vector<std::vector<HIRTypeID>>& failure_sets,
    CallableID callable,
    std::vector<std::vector<std::uint32_t>>* dependencies
) noexcept -> ControlSummary;

auto record_control(
    const SemanticConstruction& hir,
    const std::vector<std::vector<HIRTypeID>>& failure_sets
) noexcept -> RecordedControl;

auto solve_failure_contracts(SemanticConstruction& builder) noexcept
    -> std::vector<std::vector<HIRTypeID>>;

auto derive_evaluation_effects(
    const SemanticConstruction& hir,
    const RecordedControl& control
) noexcept -> std::vector<EvaluationEffect>;
