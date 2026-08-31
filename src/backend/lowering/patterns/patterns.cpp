module carven:backend.lowering.patterns.impl;

import :backend.lowering.program;
import :backend.lowering.expr;
import :backend.generation.names;
import :backend.lowering.names;
import :backend.lowering.patterns;
import :backend.lowering.stmt;
import :backend.lowering.types;
import :backend.target;
import :backend.target.expr;
import :backend.target.ids;
import :backend.target.stmt;
import :backend.target.type;
import :semantic.hir.decl;
import :semantic.hir.expr;
import :semantic.hir.ids;
import :semantic.hir.pattern;
import :semantic.hir.stmt;
import :semantic.hir.symbol;
import :semantic.hir.type;
import :support.invariant;
import std;

namespace {

auto conjunction(
    TargetUnitBuilder& builder,
    std::optional<TargetExprID> left,
    std::optional<TargetExprID> right
) noexcept -> std::optional<TargetExprID> {
    if (!left.has_value()) {
        return right;
    }
    if (!right.has_value()) {
        return left;
    }
    return builder.append_expression({
        .value = TargetBinaryExpr {
            .left = *left,
            .op = TargetBinaryOperator::LogicalAnd,
            .right = *right,
        },
    });
}

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

} // namespace

auto enumeration_for_case(const TargetCallableLowerer& context, EnumCaseID enum_case) noexcept
    -> std::pair<const HIREnumDecl*, std::size_t> {
    const auto& case_contract = context.source().enum_case(enum_case);
    const auto& enumeration = context.source().enumeration(case_contract.owner);
    const auto member = std::ranges::find(enumeration.cases, enum_case);
    if (member == enumeration.cases.end()) {
        invariant_violation("enum case pattern is absent from its owning enum contract");
    }
    return {
        &enumeration,
        static_cast<std::size_t>(std::distance(enumeration.cases.begin(), member))
    };
}

auto pattern_requires_subject(TargetCallableLowerer& context, HIRPatternID pattern_id) noexcept
    -> bool {
    const auto& value = context.source().pattern(pattern_id).value;
    if (std::holds_alternative<HIRWildcardPattern>(value)) {
        return false;
    }
    const auto* alternatives = std::get_if<HIROrPattern>(&value);
    return alternatives == nullptr
        || std::ranges::any_of(
               alternatives->alternatives,
               [&](HIRPatternID alternative_id) noexcept {
                   return pattern_requires_subject(context, alternative_id);
               }
        );
}

auto materialize_pattern_bindings(
    TargetCallableLowerer& context,
    std::span<const LoweredPatternBinding> bindings
) noexcept -> std::vector<TargetStmtID> {
    return bindings | std::views::transform([&](const auto& binding) noexcept {
               return context.target().append_lowering_statement(
                   TargetVariableStmt {
                       .binding = TargetVariableBinding::ConstValue,
                       .name = binding.name,
                       .type = binding.type,
                       .initializer = binding.initializer,
                       .maybe_unused = !symbol_is_used(context, binding.symbol),
                   }
               );
           })
        | std::ranges::to<std::vector>();
}

namespace {

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

class PatternSubjectOccurrences final {
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
    PatternSubjectOccurrences(
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

    PatternSubjectOccurrences(TargetCallableLowerer& context, TargetIdentifier name) noexcept
        : context(context),
          source(MaterializedNameSubject {.name = std::move(name)}) {}

    PatternSubjectOccurrences(TargetCallableLowerer& context, TargetExprID prototype_id) noexcept
        : context(context),
          source(
              PrototypeSubject {
                  .prototype_id = prototype_id,
                  .first_id = prototype_id,
              }
          ) {}

    PatternSubjectOccurrences(const PatternSubjectOccurrences&) = delete;
    PatternSubjectOccurrences(PatternSubjectOccurrences&&) = default;
    ~PatternSubjectOccurrences() = default;

    auto operator=(const PatternSubjectOccurrences&) -> PatternSubjectOccurrences& = delete;
    auto operator=(PatternSubjectOccurrences&&) -> PatternSubjectOccurrences& = delete;

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

auto lower_pattern_with_subject_occurrences(
    TargetCallableLowerer& context,
    PatternSubjectOccurrences* subject,
    HIRPatternID pattern_id
) noexcept -> std::vector<LoweredPattern>;

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
    auto subject_occurrences = std::optional<PatternSubjectOccurrences>();
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
            for (auto alternative : lower_pattern_with_subject_occurrences(
                     context,
                     subject_occurrences ? std::addressof(*subject_occurrences) : nullptr,
                     arm.pattern
                 )) {
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
            for (auto alternative : lower_pattern_with_subject_occurrences(
                     context,
                     subject_occurrences ? std::addressof(*subject_occurrences) : nullptr,
                     arm.pattern
                 )) {
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
        for (auto alternative : lower_pattern_with_subject_occurrences(
                 context,
                 subject_occurrences ? std::addressof(*subject_occurrences) : nullptr,
                 arm.pattern
             )) {
            const auto unmatched = context.target().append_expression({
                .value = TargetPrefixExpr {
                    .op = TargetPrefixOperator::LogicalNot,
                    .operand_id = name_expression(context, TargetName {matched_name}),
                },
            });
            const auto selected = conjunction(context.target(), unmatched, alternative.condition);
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

namespace {

auto next_pattern_subject(PatternSubjectOccurrences* subject) noexcept -> TargetExprID {
    if (subject == nullptr) {
        invariant_violation("subject-dependent pattern has no target subject");
    }
    return subject->next();
}

auto clone_pattern_occurrences(TargetUnitBuilder& builder, const LoweredPattern& pattern) noexcept
    -> LoweredPattern {
    auto bindings = std::vector<LoweredPatternBinding>();
    bindings.reserve(pattern.bindings.size());
    for (const auto& binding : pattern.bindings) {
        bindings.push_back({
            .symbol = binding.symbol,
            .name = binding.name,
            .type = binding.type,
            .initializer = builder.clone_expression_occurrence(binding.initializer),
        });
    }
    return {
        .condition = pattern.condition.transform([&](TargetExprID condition_id) noexcept {
            return builder.clone_expression_occurrence(condition_id);
        }),
        .bindings = std::move(bindings),
    };
}

auto lower_pattern_with_subject_occurrences(
    TargetCallableLowerer& context,
    PatternSubjectOccurrences* subject,
    HIRPatternID pattern_id
) noexcept -> std::vector<LoweredPattern> {
    const auto& pattern = context.source().pattern(pattern_id);
    if (std::holds_alternative<HIRWildcardPattern>(pattern.value)) {
        return {LoweredPattern {
            .condition = std::nullopt,
            .bindings = {},
        }};
    }
    if (const auto* binding = std::get_if<HIRBindingPattern>(&pattern.value)) {
        return {
            LoweredPattern {
                .condition = std::nullopt,
                .bindings = {
                    LoweredPatternBinding {
                        .symbol = binding->target.symbol,
                        .name = symbol_identifier(context, binding->target.symbol),
                        .type = lower_type(context, binding->type),
                        .initializer = next_pattern_subject(subject),
                    },
                },
            },
        };
    }
    if (const auto* literal = std::get_if<HIRLiteralPattern>(&pattern.value)) {
        const auto value = context.target().append_expression({
            .value = TargetLiteralExpr {
                .value = lower_literal(context, literal->literal, literal->type),
            },
        });
        return {LoweredPattern {
            .condition = call_expression(
                context,
                name_expression(context, TargetSymbol::RuntimeIs),
                {next_pattern_subject(subject), value}
            ),
            .bindings = {},
        }};
    }
    if (const auto* alternatives = std::get_if<HIROrPattern>(&pattern.value)) {
        auto result = std::vector<LoweredPattern>();
        for (const auto alternative : alternatives->alternatives) {
            auto lowered = lower_pattern_with_subject_occurrences(context, subject, alternative);
            result.insert(
                result.end(),
                std::make_move_iterator(lowered.begin()),
                std::make_move_iterator(lowered.end())
            );
        }
        return result;
    }
    const auto* constraint = std::get_if<HIRTypeConstraintPattern>(&pattern.value);
    if (constraint != nullptr) {
        const auto condition = context.target().append_expression({
            .value = TargetCallExpr {
                .callee = name_expression(context, TargetSymbol::RuntimeIs),
                .template_argument_type_ids = {lower_type(context, constraint->type)},
                .arguments = {next_pattern_subject(subject)},
            },
        });
        return {LoweredPattern {.condition = condition, .bindings = {}}};
    }

    const auto& case_pattern = std::get<HIRCasePattern>(pattern.value);
    const auto [enumeration, ordinal] = enumeration_for_case(context, case_pattern.enum_case);
    const auto& case_contract = context.source().enum_case(case_pattern.enum_case);
    if (case_pattern.payload.size() != case_contract.payload_types.size()) {
        invariant_violation("enum case pattern is inconsistent with its declaration");
    }
    const auto* representation_names = enumeration->profile == HIREnumProfile::Payload
        ? std::addressof(context.payload_enum(case_contract.owner))
        : nullptr;
    const auto holds = enumeration->profile == HIREnumProfile::Numeric
        ? context.target().append_expression({
              .value =
                  TargetBinaryExpr {
                      .left = next_pattern_subject(subject),
                      .op = TargetBinaryOperator::Equal,
                      .right = name_expression(
                          context,
                          symbol_reference_name(context, case_contract.symbol)
                      ),
                  },
          })
        : member_call_expression(
              context,
              next_pattern_subject(subject),
              representation_names->cases[ordinal].holds_function
          );
    auto alternatives = std::vector<LoweredPattern> {
        LoweredPattern {.condition = holds, .bindings = {}},
    };
    for (auto index = 0uz; index < case_pattern.payload.size(); ++index) {
        const auto child_pattern = case_pattern.payload[index];
        if (!pattern_requires_subject(context, child_pattern)) {
            continue;
        }
        const auto case_payload = member_call_expression(
            context,
            next_pattern_subject(subject),
            representation_names->cases[ordinal].payload_function
        );
        const auto payload = context.target().append_expression({
            .value = TargetMemberExpr {
                .operand_id = case_payload,
                .name = TargetNameAllocator::enum_payload_field(index),
            },
        });
        auto payload_occurrences = PatternSubjectOccurrences(context, payload);
        auto children = lower_pattern_with_subject_occurrences(
            context,
            std::addressof(payload_occurrences),
            child_pattern
        );
        auto product = std::vector<LoweredPattern>();
        const auto base_count = alternatives.size();
        const auto child_count = children.size();
        product.reserve(base_count * child_count);
        for (auto base_index = 0uz; base_index < base_count; ++base_index) {
            for (auto child_index = 0uz; child_index < child_count; ++child_index) {
                auto base = child_index + 1 == child_count
                    ? std::move(alternatives[base_index])
                    : clone_pattern_occurrences(context.target(), alternatives[base_index]);
                auto child = base_index + 1 == base_count
                    ? std::move(children[child_index])
                    : clone_pattern_occurrences(context.target(), children[child_index]);
                base.bindings.insert(
                    base.bindings.end(),
                    std::make_move_iterator(child.bindings.begin()),
                    std::make_move_iterator(child.bindings.end())
                );
                product.push_back({
                    .condition = conjunction(context.target(), base.condition, child.condition),
                    .bindings = std::move(base.bindings),
                });
            }
        }
        alternatives = std::move(product);
    }
    return alternatives;
}

} // namespace

auto lower_pattern(
    TargetCallableLowerer& context,
    std::optional<TargetExprID> subject_occurrence_id,
    HIRPatternID pattern_id
) noexcept -> std::vector<LoweredPattern> {
    const auto requires_subject = pattern_requires_subject(context, pattern_id);
    if (requires_subject != subject_occurrence_id.has_value()) {
        invariant_violation("target pattern subject availability does not match semantic facts");
    }
    auto occurrences = std::optional<PatternSubjectOccurrences>();
    if (subject_occurrence_id.has_value()) {
        occurrences.emplace(context, *subject_occurrence_id);
    }
    return lower_pattern_with_subject_occurrences(
        context,
        occurrences ? std::addressof(*occurrences) : nullptr,
        pattern_id
    );
}
