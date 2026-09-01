module carven:backend.lowering.match.impl;

import :backend.generation.names;
import :backend.lowering.expr;
import :backend.lowering.match;
import :backend.lowering.names;
import :backend.lowering.patterns;
import :backend.lowering.program;
import :backend.lowering.stmt;
import :backend.lowering.types;
import :backend.target;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.type;
import :semantic.hir.expr;
import :semantic.hir.pattern;
import :semantic.hir.stmt;
import :semantic.hir.type;
import :support.invariant;
import std;

namespace {

auto pattern_has_unconditional_alternative(
    TargetCallableLowerer& context,
    HIRPatternID pattern_id
) noexcept -> bool {
    const auto& value = context.source().pattern(pattern_id).value;
    if (std::holds_alternative<HIRWildcardPattern>(value)
        || std::holds_alternative<HIRBindingPattern>(value)) {
        return true;
    }
    const auto* alternatives = std::get_if<HIROrPattern>(&value);
    return alternatives != nullptr
        && std::ranges::any_of(
               alternatives->alternatives,
               [&](HIRPatternID alternative_id) noexcept {
                   return pattern_has_unconditional_alternative(context, alternative_id);
               }
        );
}

struct StatementMatch final {};
struct ValueMatch final {};
struct OutcomeMatch final {
    HIRTypeID result_type_id;
    FailureSetID failure_set_id;
};
using MatchResult = std::variant<StatementMatch, ValueMatch, OutcomeMatch>;

enum class MatchControlShape {
    ExclusiveBranches,
    GuardedReturning,
    GuardedStatement,
};

enum class MatchSubjectStoragePolicy {
    Discard,
    Reusable,
};

enum class MatchFallbackPolicy {
    None,
    Abort,
};

class MatchLoweringPlan final {
public:
    static auto seal(
        TargetCallableLowerer& context,
        std::span<const HIRMatchArm> arms,
        const HIRMatchCoverageFacts& coverage,
        bool return_result
    ) noexcept -> MatchLoweringPlan {
        if (arms.size() != coverage.arm_states.size()) {
            invariant_violation("match lowering coverage does not align with source arms");
        }

        auto reachable_arms = std::vector<std::reference_wrapper<const HIRMatchArm>>();
        reachable_arms.reserve(arms.size());
        for (auto index = 0uz; index < arms.size(); ++index) {
            if (coverage.arm_states[index] == HIRMatchArmState::Reachable) {
                reachable_arms.push_back(std::cref(arms[index]));
            }
        }

        const auto source_requires_subject =
            std::ranges::any_of(arms, [&](const HIRMatchArm& arm) noexcept {
                return pattern_requires_subject(context, arm.pattern);
            });
        const auto subject_storage = !source_requires_subject ? MatchSubjectStoragePolicy::Discard
                                                              : MatchSubjectStoragePolicy::Reusable;

        const auto sequential_control =
            std::ranges::any_of(reachable_arms, [](const auto& arm) static noexcept {
                return arm.get().guard.has_value();
            });
        const auto control_shape = !sequential_control ? MatchControlShape::ExclusiveBranches
            : return_result                            ? MatchControlShape::GuardedReturning
                                                       : MatchControlShape::GuardedStatement;

        const auto has_unconditional_arm =
            std::ranges::any_of(reachable_arms, [&](const auto& arm) noexcept {
                return !arm.get().guard.has_value()
                    && pattern_has_unconditional_alternative(context, arm.get().pattern);
            });
        const auto fallback = return_result && !has_unconditional_arm ? MatchFallbackPolicy::Abort
                                                                      : MatchFallbackPolicy::None;
        return MatchLoweringPlan(
            std::move(reachable_arms),
            control_shape,
            subject_storage,
            fallback
        );
    }

    MatchLoweringPlan(const MatchLoweringPlan&) = delete;
    MatchLoweringPlan(MatchLoweringPlan&&) = default;
    ~MatchLoweringPlan() = default;

    auto operator=(const MatchLoweringPlan&) -> MatchLoweringPlan& = delete;
    auto operator=(MatchLoweringPlan&&) -> MatchLoweringPlan& = delete;

    auto reachable_arms() const noexcept
        -> std::span<const std::reference_wrapper<const HIRMatchArm>> {
        return reachable_arm_view;
    }

    auto control_shape() const noexcept -> MatchControlShape { return control; }

    auto subject_storage() const noexcept -> MatchSubjectStoragePolicy { return storage; }

    auto fallback_policy() const noexcept -> MatchFallbackPolicy { return fallback; }

private:
    MatchLoweringPlan(
        std::vector<std::reference_wrapper<const HIRMatchArm>> reachable_arms,
        MatchControlShape control_shape,
        MatchSubjectStoragePolicy subject_storage,
        MatchFallbackPolicy fallback_policy
    ) noexcept
        : reachable_arm_view(std::move(reachable_arms)),
          control(control_shape),
          storage(subject_storage),
          fallback(fallback_policy) {}

    std::vector<std::reference_wrapper<const HIRMatchArm>> reachable_arm_view;
    MatchControlShape control;
    MatchSubjectStoragePolicy storage;
    MatchFallbackPolicy fallback;
};

class MatchSubjectOccurrences final {
    struct DeferredNameSubject final {
        HIRExprID expression_id;
        const TargetControlDestinations* control;
        std::optional<TargetExprID> prototype_id;
    };

    struct MaterializedNameSubject final {
        TargetIdentifier name;
    };

    struct PrototypeSubject final {
        TargetExprID prototype_id;
        std::optional<TargetExprID> first_id;
    };

public:
    MatchSubjectOccurrences(
        TargetCallableLowerer& context,
        HIRExprID expression_id,
        const TargetControlDestinations& control
    ) noexcept
        : context(context),
          source(
              DeferredNameSubject {
                  .expression_id = expression_id,
                  .control = std::addressof(control),
                  .prototype_id = std::nullopt,
              }
          ) {
        if (!std::holds_alternative<HIRNameExpr>(
                context.source().expression(expression_id).value
            )) {
            invariant_violation("deferred match subject is not a semantic name");
        }
    }

    MatchSubjectOccurrences(TargetCallableLowerer& context, TargetIdentifier name) noexcept
        : context(context),
          source(MaterializedNameSubject {.name = std::move(name)}) {}

    MatchSubjectOccurrences(TargetCallableLowerer& context, TargetExprID prototype_id) noexcept
        : context(context),
          source(
              PrototypeSubject {
                  .prototype_id = prototype_id,
                  .first_id = prototype_id,
              }
          ) {}

    MatchSubjectOccurrences(const MatchSubjectOccurrences&) = delete;
    MatchSubjectOccurrences(MatchSubjectOccurrences&&) = default;
    ~MatchSubjectOccurrences() = default;

    auto operator=(const MatchSubjectOccurrences&) -> MatchSubjectOccurrences& = delete;
    auto operator=(MatchSubjectOccurrences&&) -> MatchSubjectOccurrences& = delete;

    auto next() noexcept -> TargetExprID {
        if (auto* deferred = std::get_if<DeferredNameSubject>(&source)) {
            if (!deferred->prototype_id.has_value()) {
                const auto lowered =
                    lower_expression(context, deferred->expression_id, *deferred->control);
                if (!lowered.prelude.empty() || lowered.unconsumed_carrier.has_value()) {
                    invariant_violation("semantic name lowering is not an occurrence recipe");
                }
                deferred->prototype_id = lowered.expression;
                return lowered.expression;
            }
            return context.target().clone_expression_occurrence(*deferred->prototype_id);
        }
        if (const auto* materialized = std::get_if<MaterializedNameSubject>(&source)) {
            return name_expression(context, TargetName {materialized->name});
        }
        auto& prototype = std::get<PrototypeSubject>(source);
        if (prototype.first_id.has_value()) {
            const auto occurrence_id = *prototype.first_id;
            prototype.first_id.reset();
            return occurrence_id;
        }
        return context.target().clone_expression_occurrence(prototype.prototype_id);
    }

private:
    TargetCallableLowerer& context;
    std::variant<DeferredNameSubject, MaterializedNameSubject, PrototypeSubject> source;
};

auto lower_match_pattern(
    TargetCallableLowerer& context,
    std::optional<MatchSubjectOccurrences>& subject,
    HIRPatternID pattern
) noexcept -> std::vector<LoweredPattern> {
    const auto requires_subject = pattern_requires_subject(context, pattern);
    if (requires_subject && !subject.has_value()) {
        invariant_violation("subject-dependent match pattern has no target subject");
    }
    return lower_pattern(
        context,
        requires_subject ? std::optional<TargetExprID> {subject->next()} : std::nullopt,
        pattern
    );
}

auto lower_match(
    TargetCallableLowerer& context,
    HIRExprID subject_id,
    std::span<const HIRMatchArm> arms,
    const HIRMatchCoverageFacts& coverage,
    const TargetControlDestinations& control,
    MatchResult result_kind
) noexcept -> std::vector<TargetStmtID> {
    const auto* outcome = std::get_if<OutcomeMatch>(&result_kind);
    const auto return_result = !std::holds_alternative<StatementMatch>(result_kind);
    const auto plan = MatchLoweringPlan::seal(context, arms, coverage, return_result);

    auto statement_ids = std::vector<TargetStmtID>();
    auto subject_occurrences = std::optional<MatchSubjectOccurrences>();
    if (plan.subject_storage() == MatchSubjectStoragePolicy::Discard) {
        auto lowered_subject = lower_expression(context, subject_id, control);
        statement_ids = std::move(lowered_subject.prelude);
        statement_ids.push_back(context.target().append_lowering_statement(
            TargetDiscardStmt {.expression = lowered_subject.expression}
        ));
    } else if (std::holds_alternative<HIRNameExpr>(context.source().expression(subject_id).value)) {
        subject_occurrences.emplace(context, subject_id, control);
    } else {
        auto lowered_subject = lower_expression(context, subject_id, control);
        auto materialized_subject = TargetEvaluationSequencer::materialize_read_name(
            context,
            std::move(lowered_subject),
            context.source().expression(subject_id).type,
            TargetMaterializationReason::Lifetime
        );
        statement_ids = std::move(materialized_subject.prelude_ids);
        subject_occurrences.emplace(context, materialized_subject.name);
    }

    if (plan.control_shape() == MatchControlShape::ExclusiveBranches) {
        auto branches = std::vector<TargetIfBranch> {};
        auto else_body = std::optional<std::vector<TargetStmtID>> {};
        for (const auto& arm_reference : plan.reachable_arms()) {
            if (else_body.has_value()) {
                invariant_violation("exclusive match plan has a reachable arm after its fallback");
            }
            const auto& arm = arm_reference.get();
            const auto lexical_scope = context.enter_scope(arm.scope);
            for (auto alternative :
                 lower_match_pattern(context, subject_occurrences, arm.pattern)) {
                if (else_body.has_value()) {
                    invariant_violation(
                        "exclusive match plan has a reachable alternative after its fallback"
                    );
                }
                auto selected_body = materialize_pattern_bindings(context, alternative.bindings);
                auto arm_body = outcome != nullptr
                    ? lower_outcome_block(
                          context,
                          arm.body,
                          outcome->result_type_id,
                          outcome->failure_set_id,
                          control
                      )
                    : lower_block(context, arm.body, control, return_result);
                selected_body.insert(
                    selected_body.end(),
                    std::make_move_iterator(arm_body.begin()),
                    std::make_move_iterator(arm_body.end())
                );
                if (alternative.condition.has_value()) {
                    branches.push_back({
                        .condition = *alternative.condition,
                        .body = std::move(selected_body),
                    });
                } else {
                    else_body = std::move(selected_body);
                }
            }
        }
        if (branches.empty() && else_body.has_value()) {
            statement_ids.insert(
                statement_ids.end(),
                std::make_move_iterator(else_body->begin()),
                std::make_move_iterator(else_body->end())
            );
            return statement_ids;
        }
        if (plan.fallback_policy() == MatchFallbackPolicy::Abort) {
            const auto abort_call =
                call_expression(context, name_expression(context, TargetSymbol::StdAbort), {});
            else_body = std::vector<TargetStmtID> {
                context.target().append_lowering_statement(
                    TargetExprStmt {.expression = abort_call}
                ),
            };
        }
        statement_ids.push_back(context.target().append_lowering_statement(
            TargetIfStmt {
                .branches = std::move(branches),
                .else_body = std::move(else_body),
            }
        ));
        return statement_ids;
    }

    if (plan.control_shape() == MatchControlShape::GuardedReturning) {
        for (const auto& arm_reference : plan.reachable_arms()) {
            const auto& arm = arm_reference.get();
            const auto lexical_scope = context.enter_scope(arm.scope);
            for (auto alternative :
                 lower_match_pattern(context, subject_occurrences, arm.pattern)) {
                auto selected_body = materialize_pattern_bindings(context, alternative.bindings);
                auto arm_body = outcome != nullptr ? lower_outcome_block(
                                                         context,
                                                         arm.body,
                                                         outcome->result_type_id,
                                                         outcome->failure_set_id,
                                                         control
                                                     )
                                                   : lower_block(context, arm.body, control, true);
                if (arm.guard.has_value()) {
                    auto guard = lower_expression(context, *arm.guard, control);
                    guard.prelude.push_back(context.target().append_lowering_statement(
                        TargetIfStmt {
                            .branches =
                                {
                                    TargetIfBranch {
                                        .condition = guard.expression,
                                        .body = std::move(arm_body),
                                    },
                                },
                            .else_body = std::nullopt,
                        }
                    ));
                    selected_body.insert(
                        selected_body.end(),
                        std::make_move_iterator(guard.prelude.begin()),
                        std::make_move_iterator(guard.prelude.end())
                    );
                } else {
                    selected_body.insert(
                        selected_body.end(),
                        std::make_move_iterator(arm_body.begin()),
                        std::make_move_iterator(arm_body.end())
                    );
                }
                if (alternative.condition.has_value()) {
                    statement_ids.push_back(context.target().append_lowering_statement(
                        TargetIfStmt {
                            .branches =
                                {
                                    TargetIfBranch {
                                        .condition = *alternative.condition,
                                        .body = std::move(selected_body),
                                    },
                                },
                            .else_body = std::nullopt,
                        }
                    ));
                } else {
                    statement_ids.push_back(context.target().append_lowering_statement(
                        TargetBlockStmt {
                            .statements = std::move(selected_body),
                            .scoped = true,
                        }
                    ));
                }
            }
        }
        if (plan.fallback_policy() == MatchFallbackPolicy::Abort) {
            const auto abort_call =
                call_expression(context, name_expression(context, TargetSymbol::StdAbort), {});
            statement_ids.push_back(context.target().append_lowering_statement(
                TargetExprStmt {.expression = abort_call}
            ));
        }
        return statement_ids;
    }

    const auto matched_name = context.fresh_name(TargetTemporaryNameKind::MatchDone);
    const auto bool_type = intrinsic_type(context, TargetSymbol::Bool);
    const auto false_value = context.target().append_expression({
        .value = TargetLiteralExpr {.value = false},
    });
    statement_ids.push_back(context.target().append_lowering_statement(
        TargetVariableStmt {
            .binding = TargetVariableBinding::MutableValue,
            .name = matched_name,
            .type = bool_type,
            .initializer = false_value,
            .maybe_unused = false,
        }
    ));
    for (const auto& arm_reference : plan.reachable_arms()) {
        const auto& arm = arm_reference.get();
        const auto lexical_scope = context.enter_scope(arm.scope);
        for (auto alternative : lower_match_pattern(context, subject_occurrences, arm.pattern)) {
            const auto unmatched = context.target().append_expression({
                .value = TargetPrefixExpr {
                    .op = TargetPrefixOperator::LogicalNot,
                    .operand_id = name_expression(context, TargetName {matched_name}),
                },
            });
            const auto selected =
                combine_pattern_conditions(context.target(), unmatched, alternative.condition);
            auto selected_body = materialize_pattern_bindings(context, alternative.bindings);
            auto arm_body = outcome != nullptr
                ? lower_outcome_block(
                      context,
                      arm.body,
                      outcome->result_type_id,
                      outcome->failure_set_id,
                      control
                  )
                : lower_block(context, arm.body, control, return_result);
            const auto mark_matched = context.target().append_lowering_statement(
                TargetAssignmentStmt {
                    .target = name_expression(context, TargetName {matched_name}),
                    .op = TargetAssignmentOperator::Assign,
                    .value = context.target().append_expression({
                        .value = TargetLiteralExpr {.value = true},
                    }),
                }
            );
            if (arm.guard.has_value()) {
                auto guard = lower_expression(context, *arm.guard, control);
                arm_body.insert(arm_body.begin(), mark_matched);
                guard.prelude.push_back(context.target().append_lowering_statement(
                    TargetIfStmt {
                        .branches =
                            {
                                TargetIfBranch {
                                    .condition = guard.expression,
                                    .body = std::move(arm_body),
                                },
                            },
                        .else_body = std::nullopt,
                    }
                ));
                selected_body.insert(
                    selected_body.end(),
                    std::make_move_iterator(guard.prelude.begin()),
                    std::make_move_iterator(guard.prelude.end())
                );
            } else {
                selected_body.push_back(mark_matched);
                selected_body.insert(
                    selected_body.end(),
                    std::make_move_iterator(arm_body.begin()),
                    std::make_move_iterator(arm_body.end())
                );
            }
            statement_ids.push_back(context.target().append_lowering_statement(
                TargetIfStmt {
                    .branches =
                        {
                            TargetIfBranch {
                                .condition = *selected,
                                .body = std::move(selected_body),
                            },
                        },
                    .else_body = std::nullopt,
                }
            ));
        }
    }
    return statement_ids;
}

} // namespace

auto lower_statement_match(
    TargetCallableLowerer& context,
    const HIRMatchStmt& match,
    const TargetControlDestinations& control
) noexcept -> std::vector<TargetStmtID> {
    return lower_match(
        context,
        match.subject,
        match.arms,
        match.coverage,
        control,
        StatementMatch {}
    );
}

auto lower_value_match(
    TargetCallableLowerer& context,
    const HIRMatchExpr& match,
    const TargetControlDestinations& control
) noexcept -> std::vector<TargetStmtID> {
    return lower_match(context, match.subject, match.arms, match.coverage, control, ValueMatch {});
}

auto lower_outcome_match(
    TargetCallableLowerer& context,
    const HIRMatchExpr& match,
    const TargetControlDestinations& control,
    HIRTypeID result_type_id,
    FailureSetID failure_set_id
) noexcept -> std::vector<TargetStmtID> {
    return lower_match(
        context,
        match.subject,
        match.arms,
        match.coverage,
        control,
        OutcomeMatch {
            .result_type_id = result_type_id,
            .failure_set_id = failure_set_id,
        }
    );
}
