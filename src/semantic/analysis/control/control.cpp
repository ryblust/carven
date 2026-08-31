module carven:semantic.analysis.control.impl;

import :semantic.analysis.control;
import :semantic.analysis.control.internal;
import :semantic.hir.expr;
import :support.invariant;
import std;

RecordedControlAnalysis::RecordedControlAnalysis(
    std::vector<ControlSummary> expressions,
    std::vector<ControlSummary> statements,
    std::vector<ControlSummary> blocks,
    std::vector<std::vector<CatchControlSummary>> catches,
    std::vector<std::vector<HIRTypeID>> unhandled,
    std::vector<EvaluationEffect> effects,
    std::vector<std::vector<HIRTypeID>> callable_failures
) noexcept
    : expression_summaries(std::move(expressions)),
      statement_summaries(std::move(statements)),
      block_summaries(std::move(blocks)),
      catch_summaries(std::move(catches)),
      unhandled_summaries(std::move(unhandled)),
      expression_effects(std::move(effects)),
      effective_callable_failures(std::move(callable_failures)) {}

auto RecordedControlAnalysis::for_testing(
    std::vector<ControlSummary> expressions,
    std::vector<ControlSummary> statements,
    std::vector<ControlSummary> blocks,
    std::vector<std::vector<CatchControlSummary>> catches,
    std::vector<std::vector<HIRTypeID>> unhandled,
    std::vector<EvaluationEffect> effects,
    std::vector<std::vector<HIRTypeID>> callable_failures
) noexcept -> RecordedControlAnalysis {
    return RecordedControlAnalysis(
        std::move(expressions),
        std::move(statements),
        std::move(blocks),
        std::move(catches),
        std::move(unhandled),
        std::move(effects),
        std::move(callable_failures)
    );
}

auto RecordedControlAnalysis::summary(HIRExprID id) const noexcept -> const ControlSummary& {
    return expression_summaries[id.index()];
}

auto RecordedControlAnalysis::summary(HIRStmtID id) const noexcept -> const ControlSummary& {
    return statement_summaries[id.index()];
}

auto RecordedControlAnalysis::summary(HIRBlockID id) const noexcept -> const ControlSummary& {
    return block_summaries[id.index()];
}

auto RecordedControlAnalysis::catch_summary(HIRExprID id, std::size_t arm) const noexcept
    -> const CatchControlSummary& {
    return catch_summaries[id.index()][arm];
}

auto RecordedControlAnalysis::unhandled_failures(HIRExprID id) const noexcept
    -> std::span<const HIRTypeID> {
    return unhandled_summaries[id.index()];
}

auto RecordedControlAnalysis::evaluation_effect(HIRExprID id) const noexcept
    -> const EvaluationEffect& {
    return expression_effects[id.index()];
}

auto RecordedControlAnalysis::effective_failures(CallableID id) const noexcept
    -> std::span<const HIRTypeID> {
    return effective_callable_failures[id.index()];
}

auto RecordedControlAnalysis::callable_failure_sets() const noexcept
    -> std::span<const std::vector<HIRTypeID>> {
    return effective_callable_failures;
}

auto analyze_control(SemanticDraftView builder) noexcept -> RecordedControlAnalysis {
    auto failure_sets = solve_failure_contracts(builder);
    auto recorded = record_control(builder, failure_sets);
    auto effects = derive_evaluation_effects(builder);
    return RecordedControlAnalysis(
        std::move(recorded.expressions),
        std::move(recorded.statements),
        std::move(recorded.blocks),
        std::move(recorded.catches),
        std::move(recorded.unhandled),
        std::move(effects),
        std::move(failure_sets)
    );
}

auto freeze_flow_candidate(SemanticDraft& builder, RecordedControlAnalysis&& control) noexcept
    -> void {
    const auto expression_count = builder.storage.expressions.size();
    const auto block_count = builder.storage.blocks.size();
    const auto callable_count = builder.storage.callables.size();
    if (expression_count != control.expression_summaries.size()
        || expression_count != control.catch_summaries.size()
        || expression_count != control.unhandled_summaries.size()
        || expression_count != control.expression_effects.size()
        || expression_count != builder.expression_place_uses.size()
        || builder.storage.statements.size() != control.statement_summaries.size()
        || block_count != control.block_summaries.size()
        || callable_count != control.effective_callable_failures.size()
        || callable_count != builder.callable_failure_input_storage.size()) {
        invariant_violation("recorded control analysis does not cover the complete semantic graph");
    }
    if (!builder.storage.expression_controls.empty()
        || !builder.storage.evaluation_effects.empty()
        || !builder.storage.place_uses.empty()
        || !builder.storage.try_facts.empty()
        || !builder.storage.block_controls.empty()
        || !builder.storage.callable_flows.empty()) {
        invariant_violation("semantic flow candidate was frozen more than once");
    }

    for (auto& failures : control.effective_callable_failures) {
        builder.storage.callable_flows.add({
            .effective_failure_set = builder.intern_failure_set(std::move(failures)),
        });
    }

    for (auto index = 0uz; index < expression_count; ++index) {
        const auto id = HIRExprID::from_index(static_cast<std::uint32_t>(index));
        auto& summary = control.expression_summaries[index];
        auto evaluation_failures = summary.pending_failures;
        evaluation_failures.insert(
            evaluation_failures.end(),
            summary.outward_failures.begin(),
            summary.outward_failures.end()
        );
        builder.storage.expression_controls.add({
            .evaluation_failure_set = builder.intern_failure_set(std::move(evaluation_failures)),
            .exits_test = summary.transfers.exits_test,
        });
        builder.storage.evaluation_effects.add(std::move(control.expression_effects[index]));
        builder.storage.place_uses.add(std::move(builder.expression_place_uses[index]));

        auto attempt_facts = std::optional<HIRTryFacts>();
        if (const auto* attempt = std::get_if<HIRTryExpr>(&builder.expression(id).value)) {
            if (control.catch_summaries[index].size() != attempt->arms.size()) {
                invariant_violation("recorded catch analysis is not aligned with try arms");
            }
            auto arms = std::vector<HIRCatchFacts>();
            arms.reserve(control.catch_summaries[index].size());
            for (auto& source_arm : control.catch_summaries[index]) {
                auto reachable_alternative_indices = std::vector<std::uint32_t>();
                for (const auto [alternative, reachability] :
                     std::views::enumerate(source_arm.alternatives)) {
                    if (reachability == CatchAlternativeReachability::Reachable) {
                        reachable_alternative_indices.push_back(
                            static_cast<std::uint32_t>(alternative)
                        );
                    }
                }
                arms.push_back({
                    .accepted_failure_set =
                        builder.intern_failure_set(std::move(source_arm.accepted_failures)),
                    .reachable_alternative_indices = std::move(reachable_alternative_indices),
                });
            }
            attempt_facts = HIRTryFacts {
                .arms = std::move(arms),
                .unhandled_failure_set =
                    builder.intern_failure_set(std::move(control.unhandled_summaries[index])),
            };
        } else if (!control.catch_summaries[index].empty()
                   || !control.unhandled_summaries[index].empty()) {
            invariant_violation("non-try expression owns recorded catch analysis");
        }
        builder.storage.try_facts.add(std::move(attempt_facts));
    }

    for (auto& summary : control.block_summaries) {
        builder.storage.block_controls.add({
            .outward_failure_set = builder.intern_failure_set(std::move(summary.outward_failures)),
            .exits_test = summary.transfers.exits_test,
        });
    }
    builder.expression_place_uses.clear();
    builder.callable_failure_input_storage.clear();
}
