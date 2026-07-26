module carven:backend.lowering.patterns.impl;

import :backend.lowering.program;
import :backend.lowering.expressions;
import :backend.generation.names;
import :backend.lowering.names;
import :backend.lowering.patterns;
import :backend.lowering.statements;
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

} // namespace

auto enumeration_for_case(const TargetCallableLowerer& context, EnumCaseID enum_case) noexcept
    -> std::pair<const HIREnumDecl*, std::size_t> {
    const auto& case_contract = context.semantic().enum_case(enum_case);
    const auto& enumeration = context.semantic().enumeration(case_contract.owner);
    const auto member = std::ranges::find(enumeration.cases, enum_case);
    if (member == enumeration.cases.end()) {
        invariant_violation("enum case pattern is absent from its owning enum contract");
    }
    return {
        &enumeration,
        static_cast<std::size_t>(std::distance(enumeration.cases.begin(), member))
    };
}

auto pattern_requires_subject(TargetCallableLowerer& context, HIRPatternID id) noexcept -> bool {
    const auto& value = context.semantic().pattern(id).value;
    if (std::holds_alternative<HIRWildcardPattern>(value)) {
        return false;
    }
    const auto* alternatives = std::get_if<HIROrPattern>(&value);
    return alternatives == nullptr
        || std::ranges::any_of(alternatives->alternatives, [&](const auto alternative) noexcept {
               return pattern_requires_subject(context, alternative);
           });
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
    HIRTypeID result;
    FailureSetID failure_set;
};
using MatchResult = std::variant<StatementMatch, ValueMatch, OutcomeMatch>;

auto lower_match(
    TargetCallableLowerer& context,
    HIRExprID subject,
    std::span<const HIRMatchArm> arms,
    const TargetControlDestinations& control,
    MatchResult result_kind
) noexcept -> std::vector<TargetStmtID> {
    const auto* outcome = std::get_if<OutcomeMatch>(&result_kind);
    const auto return_result = !std::holds_alternative<StatementMatch>(result_kind);
    const auto subject_required = std::ranges::any_of(arms, [&](const auto& arm) noexcept {
        return pattern_requires_subject(context, arm.pattern);
    });
    auto lowered_subject = lower_expression(context, subject, control);
    const auto stable_source_name =
        std::holds_alternative<HIRNameExpr>(context.semantic().expression(subject).value);
    if (subject_required && !stable_source_name) {
        lowered_subject = TargetEvaluationSequencer::materialize(
            context,
            std::move(lowered_subject),
            TargetEvaluationSequencer::read_materialization(
                context,
                context.semantic().expression(subject).type
            ),
            TargetMaterializationReason::Lifetime
        );
    }
    const auto subject_value = lowered_subject.expression;
    auto statements = std::move(lowered_subject.prelude);
    if (!subject_required) {
        statements.push_back(context.target().append_lowering_statement(
            TargetDiscardStmt {.expression = subject_value}
        ));
    }

    const auto needs_guard_fallback =
        std::ranges::any_of(arms, [](const HIRMatchArm& arm) static noexcept {
            return arm.guard.has_value();
        });
    if (!needs_guard_fallback) {
        auto branches = std::vector<TargetIfBranch> {};
        auto else_body = std::optional<std::vector<TargetStmtID>> {};
        for (const auto& arm : arms) {
            const auto lexical_scope = context.enter_scope(arm.scope);
            for (auto alternative : lower_pattern(context, subject_value, arm.pattern)) {
                auto selected_body = materialize_pattern_bindings(context, alternative.bindings);
                auto arm_body = outcome != nullptr
                    ? lower_outcome_block(
                          context,
                          arm.body,
                          outcome->result,
                          outcome->failure_set,
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
                    break;
                }
            }
            if (else_body.has_value()) {
                break;
            }
        }
        if (branches.empty() && else_body.has_value()) {
            statements.insert(
                statements.end(),
                std::make_move_iterator(else_body->begin()),
                std::make_move_iterator(else_body->end())
            );
            return statements;
        }
        if (return_result && !else_body.has_value()) {
            const auto abort_call =
                call_expression(context, name_expression(context, TargetSymbol::StdAbort), {});
            else_body = std::vector<TargetStmtID> {
                context.target().append_lowering_statement(
                    TargetExprStmt {.expression = abort_call}
                ),
            };
        }
        statements.push_back(context.target().append_lowering_statement(
            TargetIfStmt {
                .branches = std::move(branches),
                .else_body = std::move(else_body),
            }
        ));
        return statements;
    }

    if (return_result) {
        auto terminal = false;
        for (const auto& arm : arms) {
            const auto lexical_scope = context.enter_scope(arm.scope);
            for (auto alternative : lower_pattern(context, subject_value, arm.pattern)) {
                auto selected_body = materialize_pattern_bindings(context, alternative.bindings);
                auto arm_body = outcome != nullptr ? lower_outcome_block(
                                                         context,
                                                         arm.body,
                                                         outcome->result,
                                                         outcome->failure_set,
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
                    statements.push_back(context.target().append_lowering_statement(
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
                    statements.push_back(context.target().append_lowering_statement(
                        TargetBlockStmt {
                            .statements = std::move(selected_body),
                            .scoped = true,
                        }
                    ));
                    if (!arm.guard.has_value()) {
                        terminal = true;
                        break;
                    }
                }
            }
            if (terminal) {
                break;
            }
        }
        if (!terminal) {
            const auto abort_call =
                call_expression(context, name_expression(context, TargetSymbol::StdAbort), {});
            statements.push_back(context.target().append_lowering_statement(
                TargetExprStmt {.expression = abort_call}
            ));
        }
        return statements;
    }

    const auto matched_name = context.fresh_name(TargetTemporaryNameKind::MatchDone);
    const auto matched_value = name_expression(context, TargetName {matched_name});
    const auto bool_type = intrinsic_type(context, TargetSymbol::Bool);
    const auto false_value = context.target().append_expression({
        .value = TargetLiteralExpr {.value = false},
    });
    const auto true_value = context.target().append_expression({
        .value = TargetLiteralExpr {.value = true},
    });
    statements.push_back(context.target().append_lowering_statement(
        TargetVariableStmt {
            .binding = TargetVariableBinding::MutableValue,
            .name = matched_name,
            .type = bool_type,
            .initializer = false_value,
            .maybe_unused = false,
        }
    ));
    for (const auto& arm : arms) {
        const auto lexical_scope = context.enter_scope(arm.scope);
        for (auto alternative : lower_pattern(context, subject_value, arm.pattern)) {
            const auto unmatched = context.target().append_expression({
                .value = TargetPrefixExpr {
                    .op = TargetPrefixOperator::LogicalNot,
                    .operand_id = matched_value,
                },
            });
            const auto selected = conjunction(context.target(), unmatched, alternative.condition);
            auto selected_body = materialize_pattern_bindings(context, alternative.bindings);
            auto arm_body = outcome != nullptr
                ? lower_outcome_block(
                      context,
                      arm.body,
                      outcome->result,
                      outcome->failure_set,
                      control
                  )
                : lower_block(context, arm.body, control, return_result);
            const auto mark_matched = context.target().append_lowering_statement(
                TargetAssignmentStmt {
                    .target = matched_value,
                    .op = TargetAssignmentOperator::Assign,
                    .value = true_value,
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
            statements.push_back(context.target().append_lowering_statement(
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
    return statements;
}

} // namespace

auto lower_statement_match(
    TargetCallableLowerer& context,
    HIRExprID subject,
    std::span<const HIRMatchArm> arms,
    const TargetControlDestinations& control
) noexcept -> std::vector<TargetStmtID> {
    return lower_match(context, subject, arms, control, StatementMatch {});
}

auto lower_value_match(
    TargetCallableLowerer& context,
    HIRExprID subject,
    std::span<const HIRMatchArm> arms,
    const TargetControlDestinations& control
) noexcept -> std::vector<TargetStmtID> {
    return lower_match(context, subject, arms, control, ValueMatch {});
}

auto lower_outcome_match(
    TargetCallableLowerer& context,
    HIRExprID subject,
    std::span<const HIRMatchArm> arms,
    const TargetControlDestinations& control,
    HIRTypeID result,
    FailureSetID failure_set
) noexcept -> std::vector<TargetStmtID> {
    return lower_match(
        context,
        subject,
        arms,
        control,
        OutcomeMatch {
            .result = result,
            .failure_set = failure_set,
        }
    );
}

auto lower_pattern(TargetCallableLowerer& context, TargetExprID subject, HIRPatternID id) noexcept
    -> std::vector<LoweredPattern> {
    const auto& pattern = context.semantic().pattern(id);
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
                        .initializer = subject,
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
                {subject, value}
            ),
            .bindings = {},
        }};
    }
    if (const auto* alternatives = std::get_if<HIROrPattern>(&pattern.value)) {
        auto result = std::vector<LoweredPattern>();
        for (const auto alternative : alternatives->alternatives) {
            auto lowered = lower_pattern(context, subject, alternative);
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
                .template_arguments = {lower_type(context, constraint->type)},
                .arguments = {subject},
            },
        });
        return {LoweredPattern {.condition = condition, .bindings = {}}};
    }

    const auto& case_pattern = std::get<HIRCasePattern>(pattern.value);
    const auto [enumeration, ordinal] = enumeration_for_case(context, case_pattern.enum_case);
    const auto& case_contract = context.semantic().enum_case(case_pattern.enum_case);
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
                      .left = subject,
                      .op = TargetBinaryOperator::Equal,
                      .right = name_expression(
                          context,
                          symbol_reference_name(context, case_contract.symbol)
                      ),
                  },
          })
        : member_call_expression(
              context,
              subject,
              representation_names->cases[ordinal].holds_function
          );
    auto alternatives = std::vector<LoweredPattern> {
        LoweredPattern {.condition = holds, .bindings = {}},
    };
    for (auto index = 0uz; index < case_pattern.payload.size(); ++index) {
        const auto child_pattern = case_pattern.payload[index];
        auto payload = subject;
        if (pattern_requires_subject(context, child_pattern)) {
            const auto case_payload = member_call_expression(
                context,
                subject,
                representation_names->cases[ordinal].payload_function
            );
            payload = context.target().append_expression({
                .value = TargetMemberExpr {
                    .operand_id = case_payload,
                    .name = TargetNameAllocator::enum_payload_field(index),
                },
            });
        }
        const auto children = lower_pattern(context, payload, child_pattern);
        auto product = std::vector<LoweredPattern>();
        for (const auto& base : alternatives) {
            for (const auto& child : children) {
                auto bindings = base.bindings;
                bindings.insert(bindings.end(), child.bindings.begin(), child.bindings.end());
                product.push_back({
                    .condition = conjunction(context.target(), base.condition, child.condition),
                    .bindings = std::move(bindings),
                });
            }
        }
        alternatives = std::move(product);
    }
    return alternatives;
}
