module carven:backend.lowering.expr.handlers.impl;

import :backend.lowering.expr;
import :backend.generation.names;
import :backend.lowering.names;
import :backend.lowering.patterns;
import :backend.lowering.program;
import :backend.lowering.stmt;
import :backend.lowering.types;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.type;
import :semantic.hir.expr;
import :support.invariant;
import std;

namespace {

auto conjunction(
    TargetCallableLowerer& context,
    TargetExprID left,
    std::optional<TargetExprID> right
) noexcept -> TargetExprID {
    return right.has_value() ? context.target().append_expression({
                                   .value =
                                       TargetBinaryExpr {
                                           .left = left,
                                           .op = TargetBinaryOperator::LogicalAnd,
                                           .right = *right,
                                       },
                               })
                             : left;
}

auto alternative_patterns(
    TargetCallableLowerer& context,
    const TargetIdentifier& carrier_name,
    const HIRCatchPatternAlternative& alternative,
    std::optional<TargetTypeID> failure
) noexcept -> std::vector<LoweredPattern> {
    if (!alternative.inner.has_value()) {
        return {LoweredPattern {.condition = std::nullopt, .bindings = {}}};
    }
    auto subject_occurrence_id = std::optional<TargetExprID>();
    if (pattern_requires_subject(context, *alternative.inner)) {
        subject_occurrence_id = failure.has_value()
            ? member_call_expression(
                  context,
                  name_expression(context, TargetName {carrier_name}),
                  TargetNameAllocator::fixed("failure"),
                  {*failure}
              )
            : name_expression(context, TargetName {carrier_name});
    }
    return lower_pattern(context, subject_occurrence_id, *alternative.inner);
}

auto handler_body(
    TargetCallableLowerer& context,
    HIRBlockID body,
    const TargetControlDestinations& control,
    std::optional<HIRTypeID> result,
    std::optional<FailureSetID> failure_set
) noexcept -> std::vector<TargetStmtID> {
    if (!result.has_value()) {
        return lower_block(context, body, control);
    }
    if (!failure_set.has_value() || context.failure_profile(*failure_set).ordered_members.empty()) {
        return lower_block(context, body, control, true);
    }
    return lower_outcome_block(context, body, *result, *failure_set, control);
}

auto alternative_reachable(const HIRCatchFacts& facts, std::size_t alternative) noexcept -> bool {
    return std::ranges::binary_search(
        facts.reachable_alternative_indices,
        static_cast<std::uint32_t>(alternative)
    );
}

} // namespace

auto lower_catch_handlers(
    TargetCallableLowerer& context,
    std::span<const HIRCatchArm> arms,
    std::span<const HIRCatchFacts> facts,
    const FailureCarrierDescriptor& carrier,
    TargetIdentifier carrier_name,
    const TargetControlDestinations& control,
    std::optional<HIRTypeID> result,
    std::optional<FailureSetID> failure_set
) noexcept -> LoweredCatchHandlers {
    if (arms.size() != facts.size()) {
        invariant_violation("catch lowering facts do not match the semantic arms");
    }
    const auto exclusive = std::ranges::none_of(arms, [](const HIRCatchArm& arm) static noexcept {
        return arm.guard.has_value();
    });
    if (exclusive) {
        auto branches = std::vector<TargetIfBranch> {};
        for (auto arm_index = 0uz; arm_index < arms.size(); ++arm_index) {
            const auto& arm = arms[arm_index];
            const auto lexical_scope = context.enter_scope(arm.scope);
            if (context.failure_profile(facts[arm_index].accepted_failure_set)
                    .ordered_members.empty()) {
                continue;
            }
            for (auto alternative_index = 0uz; alternative_index < arm.alternatives.size();
                 ++alternative_index) {
                if (!alternative_reachable(facts[arm_index], alternative_index)) {
                    continue;
                }
                const auto& alternative = arm.alternatives[alternative_index];
                const auto failure = alternative.type.transform([&](HIRTypeID type) noexcept {
                    return lower_type(context, type);
                });
                auto patterns = alternative_patterns(context, carrier_name, alternative, failure);
                for (auto& pattern : patterns) {
                    auto condition = failure.transform([&](TargetTypeID type_id) noexcept {
                        return member_call_expression(
                            context,
                            name_expression(context, TargetName {carrier_name}),
                            TargetNameAllocator::fixed("holds_failure"),
                            {type_id}
                        );
                    });
                    if (condition.has_value()) {
                        condition = conjunction(context, *condition, pattern.condition);
                    } else {
                        condition = pattern.condition;
                    }
                    if (!condition.has_value()) {
                        condition = context.target().append_expression({
                            .value = TargetLiteralExpr {.value = true},
                        });
                    }
                    auto selected = materialize_pattern_bindings(context, pattern.bindings);
                    const auto handler_control = control.with_caught_failure({
                        .carrier = carrier,
                        .carrier_name = carrier_name,
                        .selected_failure_type = failure,
                    });
                    auto body =
                        handler_body(context, arm.body, handler_control, result, failure_set);
                    selected.insert(
                        selected.end(),
                        std::make_move_iterator(body.begin()),
                        std::make_move_iterator(body.end())
                    );
                    branches.push_back({
                        .condition = *condition,
                        .body = std::move(selected),
                    });
                }
            }
        }
        return {
            .matched_name = std::nullopt,
            .exclusive_branches = std::move(branches),
            .statements = {},
        };
    }
    const auto expression_handlers = result.has_value();
    auto matched_name = std::optional<TargetIdentifier> {};
    auto statements = std::vector<TargetStmtID> {};
    if (!expression_handlers) {
        matched_name = context.fresh_name(TargetTemporaryNameKind::CatchDone);
        const auto false_value = context.target().append_expression({
            .value = TargetLiteralExpr {.value = false},
        });
        statements.push_back(context.target().append_lowering_statement(
            TargetVariableStmt {
                .binding = TargetVariableBinding::MutableValue,
                .name = *matched_name,
                .type = intrinsic_type(context, TargetSymbol::Bool),
                .initializer = false_value,
                .maybe_unused = false,
            }
        ));
    }
    for (auto arm_index = 0uz; arm_index < arms.size(); ++arm_index) {
        const auto& arm = arms[arm_index];
        const auto lexical_scope = context.enter_scope(arm.scope);
        if (context.failure_profile(facts[arm_index].accepted_failure_set)
                .ordered_members.empty()) {
            continue;
        }
        auto arm_branches = std::vector<TargetIfBranch>();
        for (auto alternative_index = 0uz; alternative_index < arm.alternatives.size();
             ++alternative_index) {
            if (!alternative_reachable(facts[arm_index], alternative_index)) {
                continue;
            }
            const auto& alternative = arm.alternatives[alternative_index];
            const auto failure = alternative.type.transform([&](HIRTypeID type) noexcept {
                return lower_type(context, type);
            });
            auto patterns = alternative_patterns(context, carrier_name, alternative, failure);
            for (auto& pattern : patterns) {
                auto condition = std::optional<TargetExprID> {};
                if (matched_name.has_value()) {
                    condition = context.target().append_expression({
                        .value = TargetPrefixExpr {
                            .op = TargetPrefixOperator::LogicalNot,
                            .operand_id = name_expression(context, TargetName {*matched_name}),
                        },
                    });
                }
                const auto type_condition = failure.transform([&](TargetTypeID type_id) noexcept {
                    return member_call_expression(
                        context,
                        name_expression(context, TargetName {carrier_name}),
                        TargetNameAllocator::fixed("holds_failure"),
                        {type_id}
                    );
                });
                if (type_condition.has_value()) {
                    condition = condition.has_value()
                        ? conjunction(context, *condition, type_condition)
                        : type_condition;
                }
                if (pattern.condition.has_value()) {
                    condition = condition.has_value()
                        ? conjunction(context, *condition, pattern.condition)
                        : pattern.condition;
                }
                if (!condition.has_value()) {
                    condition = context.target().append_expression({
                        .value = TargetLiteralExpr {.value = true},
                    });
                }

                auto selected = materialize_pattern_bindings(context, pattern.bindings);
                const auto handler_control = control.with_caught_failure({
                    .carrier = carrier,
                    .carrier_name = carrier_name,
                    .selected_failure_type = failure,
                });
                auto body = handler_body(context, arm.body, handler_control, result, failure_set);
                auto mark = std::optional<TargetStmtID> {};
                if (matched_name.has_value()) {
                    const auto true_value = context.target().append_expression({
                        .value = TargetLiteralExpr {.value = true},
                    });
                    mark = context.target().append_lowering_statement(
                        TargetAssignmentStmt {
                            .target = name_expression(context, TargetName {*matched_name}),
                            .op = TargetAssignmentOperator::Assign,
                            .value = true_value,
                        }
                    );
                }
                if (arm.guard.has_value()) {
                    auto guard = lower_expression(context, *arm.guard, handler_control);
                    if (mark.has_value()) {
                        body.insert(body.begin(), *mark);
                    }
                    guard.prelude.push_back(context.target().append_lowering_statement(
                        TargetIfStmt {
                            .branches = {TargetIfBranch {
                                .condition = guard.expression,
                                .body = std::move(body),
                            }},
                            .else_body = std::nullopt,
                        }
                    ));
                    selected.insert(
                        selected.end(),
                        std::make_move_iterator(guard.prelude.begin()),
                        std::make_move_iterator(guard.prelude.end())
                    );
                } else {
                    if (mark.has_value()) {
                        selected.push_back(*mark);
                    }
                    selected.insert(
                        selected.end(),
                        std::make_move_iterator(body.begin()),
                        std::make_move_iterator(body.end())
                    );
                }
                arm_branches.push_back({
                    .condition = *condition,
                    .body = std::move(selected),
                });
            }
        }
        if (!arm_branches.empty()) {
            statements.push_back(context.target().append_lowering_statement(
                TargetIfStmt {
                    .branches = std::move(arm_branches),
                    .else_body = std::nullopt,
                }
            ));
        }
    }
    return {
        .matched_name = matched_name,
        .exclusive_branches = {},
        .statements = std::move(statements),
    };
}

auto complete_catch_handlers(
    TargetCallableLowerer& context,
    LoweredCatchHandlers handlers,
    std::vector<TargetStmtID> unmatched
) noexcept -> std::vector<TargetStmtID> {
    if (!handlers.exclusive_branches.empty()) {
        return {
            context.target().append_statement({
                .value =
                    TargetIfStmt {
                        .branches = std::move(handlers.exclusive_branches),
                        .else_body = unmatched.empty()
                            ? std::nullopt
                            : std::optional<std::vector<TargetStmtID>> {std::move(unmatched)},
                    },
                .attribution = {
                    .kind = TargetAttributionKind::SourceExpansion,
                    .origin = std::nullopt,
                    .reason = TargetSyntheticReason::FailureTransport,
                },
            }),
        };
    }
    if (!handlers.matched_name.has_value()) {
        handlers.statements.insert(
            handlers.statements.end(),
            std::make_move_iterator(unmatched.begin()),
            std::make_move_iterator(unmatched.end())
        );
        return std::move(handlers.statements);
    }
    const auto unmatched_condition = context.target().append_expression({
        .value = TargetPrefixExpr {
            .op = TargetPrefixOperator::LogicalNot,
            .operand_id = name_expression(context, TargetName {*handlers.matched_name}),
        },
    });
    handlers.statements.push_back(context.target().append_statement({
        .value =
            TargetIfStmt {
                .branches = {TargetIfBranch {
                    .condition = unmatched_condition,
                    .body = std::move(unmatched),
                }},
                .else_body = std::nullopt,
            },
        .attribution = {
            .kind = TargetAttributionKind::SourceExpansion,
            .origin = std::nullopt,
            .reason = TargetSyntheticReason::FailureTransport,
        },
    }));
    return std::move(handlers.statements);
}
