module carven:semantic.analysis.control.impl;

import :semantic.analysis.control;
import :semantic.analysis.control.internal;
import :semantic.hir.expr;
import :support.invariant;
import std;

SolvedControl::SolvedControl(
    std::vector<ControlSummary> expressions,
    std::vector<ControlSummary> statements,
    std::vector<ControlSummary> blocks,
    std::vector<std::vector<std::vector<HIRTypeID>>> catches,
    std::vector<std::vector<HIRTypeID>> unhandled,
    std::vector<EvaluationEffect> effects
) noexcept
    : expression_summaries(std::move(expressions)),
      statement_summaries(std::move(statements)),
      block_summaries(std::move(blocks)),
      catch_summaries(std::move(catches)),
      unhandled_summaries(std::move(unhandled)),
      expression_effects(std::move(effects)) {}

auto SolvedControl::summary(HIRExprID id) const noexcept -> const ControlSummary& {
    return expression_summaries[id.index()];
}

auto SolvedControl::summary(HIRStmtID id) const noexcept -> const ControlSummary& {
    return statement_summaries[id.index()];
}

auto SolvedControl::summary(HIRBlockID id) const noexcept -> const ControlSummary& {
    return block_summaries[id.index()];
}

auto SolvedControl::catch_failures(HIRExprID id, std::size_t arm) const noexcept
    -> std::span<const HIRTypeID> {
    return catch_summaries[id.index()][arm];
}

auto SolvedControl::unhandled_failures(HIRExprID id) const noexcept -> std::span<const HIRTypeID> {
    return unhandled_summaries[id.index()];
}

auto SolvedControl::evaluation_effect(HIRExprID id) const noexcept -> const EvaluationEffect& {
    return expression_effects[id.index()];
}

auto solve_control(SemanticConstruction& builder) noexcept -> SolvedControl {
    auto failure_sets = solve_failure_contracts(builder);
    for (auto index = 0uz; index < builder.callables().size(); ++index) {
        const auto callable = CallableID::from_index(static_cast<std::uint32_t>(index));
        builder.set_callable_failures(callable, builder.intern_failure_set(failure_sets[index]));
    }

    auto recorded = record_control(builder, failure_sets);
    auto effects = derive_evaluation_effects(builder, recorded);
    return SolvedControl(
        std::move(recorded.expressions),
        std::move(recorded.statements),
        std::move(recorded.blocks),
        std::move(recorded.catches),
        std::move(recorded.unhandled),
        std::move(effects)
    );
}

auto commit_control_facts(SemanticConstruction& builder, SolvedControl&& control) noexcept -> void {
    if (builder.expressions().size() != control.expression_summaries.size()
        || builder.expressions().size() != control.catch_summaries.size()
        || builder.expressions().size() != control.expression_effects.size()
        || builder.statements().size() != control.statement_summaries.size()
        || builder.blocks().size() != control.block_summaries.size()) {
        invariant_violation("solved control does not cover the complete body tree");
    }
    auto expression_facts = std::vector<HIRExpressionFacts>();
    expression_facts.reserve(control.expression_summaries.size());
    for (auto index = 0uz; index < control.expression_summaries.size(); ++index) {
        const auto id = HIRExprID::from_index(static_cast<std::uint32_t>(index));
        auto& source = control.expression_summaries[index];
        auto evaluation_failures = source.pending_failures;
        evaluation_failures.insert(
            evaluation_failures.end(),
            source.outward_failures.begin(),
            source.outward_failures.end()
        );
        auto attempt = std::optional<HIRTryFacts>();
        if (std::holds_alternative<HIRTryExpr>(builder.expression(id).value)) {
            auto arms = std::vector<HIRCatchFacts>();
            arms.reserve(control.catch_summaries[index].size());
            for (auto& accepted : control.catch_summaries[index]) {
                arms.push_back(
                    {.accepted_failure_set = builder.intern_failure_set(std::move(accepted))}
                );
            }
            attempt = HIRTryFacts {
                .arms = std::move(arms),
                .unhandled_failure_set =
                    builder.intern_failure_set(std::move(control.unhandled_summaries[index])),
            };
        }
        expression_facts.push_back({
            .pending_failure_set = builder.intern_failure_set(std::move(source.pending_failures)),
            .outward_failure_set = builder.intern_failure_set(std::move(source.outward_failures)),
            .evaluation_failure_set = builder.intern_failure_set(std::move(evaluation_failures)),
            .exits_test = source.transfers.exits_test,
            .place_use = builder.place_use(id),
            .evaluation_effect = std::move(control.expression_effects[index]),
            .attempt = std::move(attempt),
        });
    }
    auto block_facts = std::vector<HIRBlockFacts>();
    block_facts.reserve(control.block_summaries.size());
    for (auto& source : control.block_summaries) {
        block_facts.push_back({
            .outward_failure_set = builder.intern_failure_set(std::move(source.outward_failures)),
            .exits_test = source.transfers.exits_test,
        });
    }
    builder.publish_control_facts(std::move(expression_facts), std::move(block_facts));
}
