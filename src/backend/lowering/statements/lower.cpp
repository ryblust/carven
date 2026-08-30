module carven:backend.lowering.statements.lower.impl;

import :backend.lowering.program;
import :backend.lowering.expressions;
import :backend.generation.names;
import :backend.lowering.names;
import :backend.lowering.patterns;
import :backend.lowering.statements;
import :backend.lowering.types;
import :backend.target.expr;
import :backend.target.ids;
import :backend.target.raw;
import :backend.target.stmt;
import :semantic.hir.expr;
import :semantic.hir.ids;
import :semantic.hir.stmt;
import :support.visit;
import std;

auto lower_statement(
    TargetCallableLowerer& context,
    HIRStmtID id,
    const TargetControlDestinations& control
) noexcept -> TargetStmtID {
    const auto& source = context.semantic().statement(id);
    auto value = lower_statement_value(context, source, control);
    return context.target().append_statement({
        .value = std::move(value),
        .attribution = {
            .kind = std::holds_alternative<HIRCppStmt>(source.value)
                ? TargetAttributionKind::RawSource
                : TargetAttributionKind::SourceOwned,
            .origin = target_source_origin(context, source.origin),
            .reason = std::nullopt,
        },
    });
}

auto with_prelude(
    TargetCallableLowerer& context,
    std::vector<TargetStmtID> prelude,
    TargetStmtValue value
) noexcept -> TargetStmtValue {
    if (prelude.empty()) {
        return value;
    }
    prelude.push_back(context.target().append_lowering_statement(std::move(value)));
    return TargetBlockStmt {
        .statements = std::move(prelude),
        .scoped = false,
    };
}

auto lower_statement_value(
    TargetCallableLowerer& context,
    const HIRStmt& source,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue {
    return std::visit(
        [&](const auto& value) noexcept -> TargetStmtValue {
            using Value = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Value, HIRCppStmt>) {
                return lower_statement(context, value);
            } else if constexpr (std::same_as<Value, HIRTestCheckStmt>
                                 || std::same_as<Value, HIRTestRequireStmt>) {
                return lower_statement(
                    context,
                    value,
                    context.semantic().expression(value.condition).origin,
                    control
                );
            } else if constexpr (std::same_as<Value, HIRTestFailStmt>) {
                return lower_statement(context, value, source.origin, control);
            } else {
                return lower_statement(context, value, control);
            }
        },
        source.value
    );
}

auto lower_statement(
    TargetCallableLowerer& context,
    const HIRIfStmt& statement,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue {
    auto alternate = statement.else_branch.has_value()
        ? lower_block(context, *statement.else_branch, control)
        : std::vector<TargetStmtID>();
    for (auto index = statement.branches.size(); index > 0; --index) {
        const auto& branch = statement.branches[index - 1];
        auto condition = lower_expression(context, branch.condition, control);
        auto branch_body = lower_block(context, branch.body, control);
        auto else_body = alternate.empty()
            ? std::optional<std::vector<TargetStmtID>>()
            : std::optional<std::vector<TargetStmtID>> {std::move(alternate)};
        const auto branch_statement = context.target().append_lowering_statement(
            TargetIfStmt {
                .branches =
                    {
                        TargetIfBranch {
                            .condition = condition.expression,
                            .body = std::move(branch_body),
                        },
                    },
                .else_body = std::move(else_body),
            }
        );
        condition.prelude.push_back(branch_statement);
        alternate = std::move(condition.prelude);
    }
    return TargetBlockStmt {
        .statements = std::move(alternate),
        .scoped = false,
    };
}

auto lower_statement(
    TargetCallableLowerer& context,
    const HIRMatchStmt& statement,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue {
    return TargetBlockStmt {
        .statements = lower_statement_match(context, statement.subject, statement.arms, control),
        .scoped = true,
    };
}

auto lower_void_try(
    TargetCallableLowerer& context,
    HIRExprID id,
    const HIRTryExpr& expression,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue {
    const auto& facts = *context.semantic().expression_facts(id).attempt;
    const auto protected_failure_set =
        context.semantic().block_facts(expression.body).outward_failure_set;
    if (context.failure_set(protected_failure_set).ordered_members.empty()) {
        return TargetBlockStmt {
            .statements = lower_block(context, expression.body, control),
            .scoped = true,
        };
    }
    const auto outcome_name = context.fresh_name(TargetTemporaryNameKind::Try);
    const auto outcome = name_expression(context, TargetName {outcome_name});
    const auto transfer_label = context.fresh_name(TargetTemporaryNameKind::Region);
    const auto protected_carrier = make_failure_carrier(
        context,
        context.semantic().expression(id).type,
        protected_failure_set
    );
    const auto initial_outcome = outcome_success_expression(context, protected_carrier.type);
    auto statements = std::vector<TargetStmtID> {
        context.target().append_lowering_statement(
            TargetVariableStmt {
                .binding = TargetVariableBinding::MutableValue,
                .name = outcome_name,
                .type = intrinsic_type(context, TargetSymbol::Auto),
                .initializer = initial_outcome,
                .maybe_unused = false,
            }
        ),
    };
    const auto protected_control = control.with_failure(
        FailureContinuation {
            .carrier = protected_carrier,
            .destination = outcome,
            .transfer_label = transfer_label,
        }
    );
    const auto protected_body = lower_block(context, expression.body, protected_control);
    const auto protected_block = context.target().append_lowering_statement(
        TargetBlockStmt {.statements = protected_body, .scoped = true}
    );
    const auto transfer_target = context.target().append_lowering_statement(
        TargetLabelStmt {
            .label = transfer_label,
            .kind = TargetSyntheticControlKind::StatementTryFailureForward,
        }
    );
    const auto protected_region = context.target().append_lowering_statement(
        TargetBlockStmt {
            .statements = {protected_block, transfer_target},
            .scoped = false,
        }
    );
    statements.push_back(protected_region);
    const auto failed = context.target().append_expression({
        .value = TargetPrefixExpr {
            .op = TargetPrefixOperator::LogicalNot,
            .operand_id =
                member_call_expression(context, outcome, TargetNameAllocator::fixed("has_value")),
        },
    });
    auto handlers = lower_catch_handlers(
        context,
        expression.arms,
        facts.arms,
        protected_carrier,
        outcome,
        control
    );
    auto catch_body = complete_catch_handlers(
        context,
        std::move(handlers),
        lower_unhandled_failure_transfer(context, outcome, facts.unhandled_failure_set, control)
    );
    const auto failed_handler = context.target().append_lowering_statement(
        TargetIfStmt {
            .branches =
                {
                    TargetIfBranch {
                        .condition = failed,
                        .body = std::move(catch_body),
                    },
                },
            .else_body = std::nullopt,
        }
    );
    statements.push_back(failed_handler);
    return TargetBlockStmt {
        .statements = std::move(statements),
        .scoped = true,
    };
}

namespace {

auto test_origin_arguments(TargetCallableLowerer& context, ProgramOriginID origin) noexcept
    -> std::vector<TargetExprID> {
    const auto& semantic_origin = context.semantic().provenance().origin(origin);
    const auto& source = context.semantic().provenance().source_snapshot(semantic_origin.source_id);
    const auto location = context.semantic().provenance().location(origin);
    return {
        context.target().append_expression({
            .value =
                TargetLiteralExpr {
                    .value =
                        TargetStringLiteral {
                            .bytes = std::string(source.display_origin()),
                            .kind = TargetStringLiteralKind::String,
                        },
                },
        }),
        context.target().append_expression({
            .value = TargetLiteralExpr {
                .value = TargetIntegerLiteral {
                    .negative = false,
                    .magnitude = location.line,
                    .suffix = TargetIntegerSuffix::None,
                },
            },
        }),
    };
}

auto lower_test_message(
    TargetCallableLowerer& context,
    std::optional<HIRExprID> message,
    const TargetControlDestinations& control,
    std::vector<TargetStmtID>& statements
) noexcept -> std::optional<TargetExprID> {
    if (!message.has_value()) {
        return std::nullopt;
    }
    auto lowered = lower_expression(context, *message, control);
    statements.insert(
        statements.end(),
        std::make_move_iterator(lowered.prelude.begin()),
        std::make_move_iterator(lowered.prelude.end())
    );
    const auto name = context.fresh_name(TargetTemporaryNameKind::TestValue);
    const auto value = name_expression(context, TargetName {name});
    statements.push_back(context.target().append_lowering_statement(
        TargetVariableStmt {
            .binding = TargetVariableBinding::ConstValue,
            .name = name,
            .type = intrinsic_type(context, TargetSymbol::Auto),
            .initializer = lowered.expression,
            .maybe_unused = false,
        }
    ));
    return value;
}

template<typename Operation>
auto lower_condition_test(
    TargetCallableLowerer& context,
    const Operation& operation,
    ProgramOriginID origin,
    std::string_view operation_name,
    bool exits_on_failure,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue {
    auto condition = lower_expression(context, operation.condition, control);
    auto statements = std::move(condition.prelude);
    const auto condition_name = context.fresh_name(TargetTemporaryNameKind::TestValue);
    const auto condition_value = name_expression(context, TargetName {condition_name});
    statements.push_back(context.target().append_lowering_statement(
        TargetVariableStmt {
            .binding = TargetVariableBinding::ConstValue,
            .name = condition_name,
            .type = intrinsic_type(context, TargetSymbol::Auto),
            .initializer = condition.expression,
            .maybe_unused = false,
        }
    ));
    const auto message = lower_test_message(context, operation.message, control, statements);
    auto arguments = std::vector<TargetExprID>();
    auto metadata = test_origin_arguments(context, origin);
    arguments.insert(arguments.end(), metadata.begin(), metadata.end());
    arguments.push_back(context.target().append_expression({
        .value = TargetLiteralExpr {
            .value = TargetStringLiteral {
                .bytes = std::string(operation_name),
                .kind = TargetStringLiteralKind::StringView,
            },
        },
    }));
    arguments.push_back(context.target().append_expression({
        .value = TargetLiteralExpr {
            .value = TargetStringLiteral {
                .bytes = std::string(
                    context.semantic().provenance().spelling(operation.condition_source)
                ),
                .kind = TargetStringLiteralKind::StringView,
            },
        },
    }));
    arguments.push_back(
        message.has_value() ? *message : name_expression(context, TargetSymbol::StdNullopt)
    );
    const auto report = call_expression(
        context,
        name_expression(context, TargetSymbol::TestingReportFailure),
        std::move(arguments)
    );
    auto failure_body = std::vector<TargetStmtID> {
        context.target().append_lowering_statement(TargetExprStmt {.expression = report}),
    };
    if (exits_on_failure) {
        failure_body.push_back(test_exit_statement(context, control));
    }
    const auto failed = context.target().append_expression({
        .value = TargetPrefixExpr {
            .op = TargetPrefixOperator::LogicalNot,
            .operand_id = condition_value,
        },
    });
    statements.push_back(context.target().append_lowering_statement(
        TargetIfStmt {
            .branches =
                {
                    TargetIfBranch {
                        .condition = failed,
                        .body = std::move(failure_body),
                    },
                },
            .else_body = std::nullopt,
        }
    ));
    return TargetBlockStmt {.statements = std::move(statements), .scoped = true};
}

} // namespace

auto lower_statement(
    TargetCallableLowerer& context,
    const HIRTestCheckStmt& statement,
    ProgramOriginID origin,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue {
    return lower_condition_test(context, statement, origin, "check", false, control);
}

auto lower_statement(
    TargetCallableLowerer& context,
    const HIRTestRequireStmt& statement,
    ProgramOriginID origin,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue {
    return lower_condition_test(context, statement, origin, "require", true, control);
}

auto lower_statement(
    TargetCallableLowerer& context,
    const HIRTestFailStmt& statement,
    ProgramOriginID origin,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue {
    auto statements = std::vector<TargetStmtID>();
    const auto message = lower_test_message(context, statement.message, control, statements);
    auto arguments = std::vector<TargetExprID>();
    auto metadata = test_origin_arguments(context, origin);
    arguments.insert(arguments.end(), metadata.begin(), metadata.end());
    arguments.push_back(context.target().append_expression({
        .value = TargetLiteralExpr {
            .value = TargetStringLiteral {
                .bytes = "fail",
                .kind = TargetStringLiteralKind::StringView,
            },
        },
    }));
    arguments.push_back(name_expression(context, TargetSymbol::StdNullopt));
    arguments.push_back(
        message.has_value() ? *message : name_expression(context, TargetSymbol::StdNullopt)
    );
    const auto report = call_expression(
        context,
        name_expression(context, TargetSymbol::TestingReportFailure),
        std::move(arguments)
    );
    statements.push_back(
        context.target().append_lowering_statement(TargetExprStmt {.expression = report})
    );
    statements.push_back(test_exit_statement(context, control));
    return TargetBlockStmt {.statements = std::move(statements), .scoped = true};
}

auto lower_statement(const TargetCallableLowerer& context, const HIRCppStmt& statement) noexcept
    -> TargetStmtValue {
    return TargetRawFragment {
        .bytes = std::string(context.semantic().provenance().spelling(statement.bytes)),
    };
}
